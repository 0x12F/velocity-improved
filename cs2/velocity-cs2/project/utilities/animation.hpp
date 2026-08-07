#pragma once

namespace animation {

	enum class easing : std::uint8_t
	{
		linear,
		ease_in,
		ease_out,
		ease_in_out
	};

	class tween
	{
	public:
		void start( float from, float to, float duration, easing ease = easing::ease_out )
		{
			this->m_from = from;
			this->m_to = to;
			this->m_value = from;
			this->m_duration = duration;
			this->m_elapsed = 0.0f;
			this->m_easing = ease;
			this->m_finished = false;
		}

		void update( )
		{
			if ( this->m_finished )
			{
				return;
			}

			const auto dt = xdraw::delta_time( );

			this->m_elapsed += dt;

			if ( this->m_elapsed >= this->m_duration )
			{
				this->m_value = this->m_to;
				this->m_finished = true;
				return;
			}

			const auto t = this->apply_easing( this->m_elapsed / this->m_duration );
			this->m_value = this->m_from + ( this->m_to - this->m_from ) * t;
		}

		[[nodiscard]] float value( ) const { return this->m_value; }
		[[nodiscard]] bool finished( ) const { return this->m_finished; }

		void reset( )
		{
			this->m_value = this->m_from;
			this->m_elapsed = 0.0f;
			this->m_finished = true;
		}

	private:
		[[nodiscard]] float apply_easing( float t ) const
		{
			switch ( this->m_easing )
			{
			case easing::ease_in:
				return t * t;

			case easing::ease_out:
				return 1.0f - ( 1.0f - t ) * ( 1.0f - t );

			case easing::ease_in_out:
				return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow( -2.0f * t + 2.0f, 2.0f ) * 0.5f;

			default:
				return t;
			}
		}

		float m_from{ 0.0f };
		float m_to{ 0.0f };
		float m_value{ 0.0f };
		float m_duration{ 0.0f };
		float m_elapsed{ 0.0f };
		easing m_easing{ easing::linear };
		bool m_finished{ true };
	};

	class tween2d
	{
	public:
		void start( float from_x, float from_y, float to_x, float to_y, float duration, easing ease = easing::ease_out )
		{
			this->m_x.start( from_x, to_x, duration, ease );
			this->m_y.start( from_y, to_y, duration, ease );
		}

		void update( )
		{
			this->m_x.update( );
			this->m_y.update( );
		}

		[[nodiscard]] float x( ) const { return this->m_x.value( ); }
		[[nodiscard]] float y( ) const { return this->m_y.value( ); }
		[[nodiscard]] bool finished( ) const { return this->m_x.finished( ) && this->m_y.finished( ); }

		void reset( )
		{
			this->m_x.reset( );
			this->m_y.reset( );
		}

	private:
		tween m_x{};
		tween m_y{};
	};

	class spring
	{
	public:
		void set_target( float target )
		{
			this->m_target = target;
		}

		void update( )
		{
			const auto dt = xdraw::delta_time( );

			const auto diff = this->m_target - this->m_value;
			const auto accel = diff * this->m_stiffness - this->m_velocity * this->m_damping;

			this->m_velocity += accel * dt;
			this->m_value += this->m_velocity * dt;
		}

		[[nodiscard]] float value( ) const { return this->m_value; }

		[[nodiscard]] bool settled( ) const
		{
			return std::abs( this->m_target - this->m_value ) < 0.001f && std::abs( this->m_velocity ) < 0.001f;
		}

		void set_stiffness( float stiffness ) { this->m_stiffness = stiffness; }
		void set_damping( float damping ) { this->m_damping = damping; }

		void snap( float value )
		{
			this->m_value = value;
			this->m_target = value;
			this->m_velocity = 0.0f;
		}

	private:
		float m_value{ 0.0f };
		float m_velocity{ 0.0f };
		float m_target{ 0.0f };
		float m_stiffness{ 200.0f };
		float m_damping{ 20.0f };
	};

	class spring2d
	{
	public:
		void set_target( float x, float y )
		{
			this->m_x.set_target( x );
			this->m_y.set_target( y );
		}

		void update( )
		{
			this->m_x.update( );
			this->m_y.update( );
		}

		[[nodiscard]] float x( ) const { return this->m_x.value( ); }
		[[nodiscard]] float y( ) const { return this->m_y.value( ); }
		[[nodiscard]] bool settled( ) const { return this->m_x.settled( ) && this->m_y.settled( ); }

		void set_stiffness( float stiffness )
		{
			this->m_x.set_stiffness( stiffness );
			this->m_y.set_stiffness( stiffness );
		}

		void set_damping( float damping )
		{
			this->m_x.set_damping( damping );
			this->m_y.set_damping( damping );
		}

		void snap( float x, float y )
		{
			this->m_x.snap( x );
			this->m_y.snap( y );
		}

	private:
		spring m_x{};
		spring m_y{};
	};

	class progress
	{
	public:
		void set( float target, float duration = 0.3f )
		{
			this->m_tween.start( this->m_tween.value( ), target, duration, easing::ease_out );
			this->m_target = target;
		}

		void update( )
		{
			this->m_tween.update( );
		}

		[[nodiscard]] float value( ) const { return this->m_tween.value( ); }
		[[nodiscard]] float target( ) const { return this->m_target; }
		[[nodiscard]] bool finished( ) const { return this->m_tween.finished( ); }

	private:
		tween m_tween{};
		float m_target{ 0.0f };
	};

	class fade
	{
	public:
		void fade_in( float duration = 0.2f )
		{
			this->m_tween.start( this->m_tween.value( ), 1.0f, duration, easing::ease_out );
			this->m_alpha_target = 1.0f;
		}

		void fade_out( float duration = 0.2f )
		{
			this->m_tween.start( this->m_tween.value( ), 0.0f, duration, easing::ease_out );
			this->m_alpha_target = 0.0f;
		}

		void update( )
		{
			this->m_tween.update( );
		}

		[[nodiscard]] float alpha( ) const { return this->m_tween.value( ); }
		[[nodiscard]] bool visible( ) const { return this->m_alpha_target > 0.0f || !this->m_tween.finished( ); }
		[[nodiscard]] bool finished( ) const { return this->m_tween.finished( ); }

	private:
		tween m_tween{};
		float m_alpha_target{ 0.0f };
	};

} // namespace animation