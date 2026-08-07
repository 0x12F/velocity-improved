#pragma once

namespace threadpool {

	enum class job_priority : std::uint8_t
	{
		low = 0,
		normal = 1,
		high = 2,
		urgent = 3,
	};

	enum class job_status : int
	{
		idle = 0,
		queued = 1,
		running = 2,
		aborted = 3,
		reset = 4,
	};

	class job
	{
	public:
		job( ) = default;
		explicit job( std::uintptr_t ptr, bool add_reference = true );
		~job( );

		job( const job& o );
		job& operator=( const job& o );
		job( job&& o ) noexcept;
		job& operator=( job&& o ) noexcept;

		[[nodiscard]] bool valid( ) const { return this->m_ptr != 0; }
		[[nodiscard]] std::uintptr_t get( ) const { return this->m_ptr; }
		[[nodiscard]] explicit operator bool( ) const { return this->valid( ); }

		[[nodiscard]] job_status status( ) const;
		[[nodiscard]] bool complete( ) const;
		void wait( ) const;

	private:
		void add_ref( ) const;
		void do_release( ) const;

		std::uintptr_t m_ptr{};
	};

	class pool
	{
	public:
		pool( ) = default;
		explicit pool( std::uintptr_t instance ) : m_instance( instance ) {}

		[[nodiscard]] bool valid( ) const { return this->m_instance != 0; }
		[[nodiscard]] std::uintptr_t get( ) const { return this->m_instance; }

		void add_job( std::uintptr_t job ) const;

	private:
		std::uintptr_t m_instance{};
	};

	bool initialize( );

	std::uintptr_t make_job( std::function<void( )>&& func, job_priority priority = job_priority::normal, const char* debug_name = nullptr );
	job run( std::function<void( )> func, job_priority priority = job_priority::normal );
	void run_sync( std::function<void( )> func, job_priority priority = job_priority::normal );
	void parallel_for( int begin, int end, const std::function<void( int, int )>& body, int min_chunk_size = 1, job_priority priority = job_priority::normal );
	void run_batch( std::span<std::function<void( )>> tasks, job_priority priority = job_priority::normal );

} // namespace threadpool