#include "memory.hpp"
#include <pch/pch.hpp>

namespace memory
{
    // MSVC x64 RTTI uses image-relative addresses in these descriptors.
    struct CompleteObjectLocator
    {
        std::uint32_t signature;
        std::uint32_t offset;
        std::uint32_t constructorDisplacement;
        std::uint32_t typeDescriptorRva;
        std::uint32_t hierarchyRva;
        std::uint32_t selfRva;
    };

    struct ClassHierarchyDescriptor
    {
        std::uint32_t signature;
        std::uint32_t attributes;
        std::uint32_t baseCount;
        std::uint32_t baseArrayRva;
    };

    struct BaseClassDescriptor
    {
        std::uint32_t typeDescriptorRva;
        std::uint32_t containedBases;
        std::int32_t memberDisplacement;
        std::int32_t vbtableDisplacement;
        std::int32_t virtualDisplacement;
        std::uint32_t attributes;
        std::uint32_t hierarchyRva;
    };

    struct TypeDescriptor
    {
        const void *vtable;
        void *spare;
        char name[1];
    };

    void *find_nonvirtual_base(void *object, const char *typeName)
    {
        if (!object || !typeName)
            return nullptr;

        __try
        {
            const auto *vtable = *reinterpret_cast<const std::uintptr_t *const *>(object);
            if (!vtable)
                return nullptr;

            const auto *locator = reinterpret_cast<const CompleteObjectLocator *>(vtable[-1]);
            if (!locator || locator->signature != 1 || !locator->selfRva || !locator->hierarchyRva || locator->constructorDisplacement != 0)
                return nullptr;

            const std::uintptr_t imageBase = reinterpret_cast<std::uintptr_t>(locator) - locator->selfRva;
            const auto *hierarchy = reinterpret_cast<const ClassHierarchyDescriptor *>(imageBase + locator->hierarchyRva);
            if (!hierarchy->baseArrayRva || hierarchy->baseCount == 0 || hierarchy->baseCount > 1024)
                return nullptr;

            const auto *bases = reinterpret_cast<const std::uint32_t *>(imageBase + hierarchy->baseArrayRva);
            auto *completeObject = static_cast<std::byte *>(object) - locator->offset;
            void *result = nullptr;

            for (std::uint32_t i = 0; i < hierarchy->baseCount; ++i)
            {
                if (!bases[i])
                    return nullptr;

                const auto *base = reinterpret_cast<const BaseClassDescriptor *>(imageBase + bases[i]);
                if (!base->typeDescriptorRva)
                    return nullptr;

                const auto *type = reinterpret_cast<const TypeDescriptor *>(imageBase + base->typeDescriptorRva);
                if (std::strcmp(type->name, typeName) != 0)
                    continue;

                // The supported model entities use a unique, non-virtual owner base.
                if (base->vbtableDisplacement != -1 || base->memberDisplacement < 0 || (base->attributes & 0x2u))
                    return nullptr;

                void *candidate = completeObject + base->memberDisplacement;
                if (result && result != candidate)
                    return nullptr;

                result = candidate;
            }

            return result;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
} // namespace memory
