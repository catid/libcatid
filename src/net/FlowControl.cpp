#include <cat/net/FlowControl.hpp>
#include <cat/time/Clock.hpp>
#include <cat/io/Logging.hpp>
#include <cat/net/SphynxTransport.hpp>
using namespace cat;
using namespace sphynx;

FlowControl::FlowControl()
{
	_max_epoch_bytes = MIN_RATE_LIMIT / 2;

	_send_epoch_bytes = 0;

	_next_epoch_time = Clock::msec();
}

void FlowControl::OnTick(u32 now)
{
	// If epoch has ended,
	if ((s32)(now - _next_epoch_time) >= 0)
	{
		INFO("FlowControl") << "_send_epoch_bytes = " << _send_epoch_bytes;

		// If some bandwidth has been used this epoch,
		if ((s32)_send_epoch_bytes > 0)
		{
			// Subtract off the amount allowed this epoch
			Atomic::Add(&_send_epoch_bytes, -_max_epoch_bytes);
		}

		// Set next epoch time
		_next_epoch_time += EPOCH_INTERVAL;

		// If within one tick of another epoch,
		if ((s32)(now - _next_epoch_time + sphynx::Transport::TICK_INTERVAL) > 0)
		{
			WARN("FlowControl") << "Slow epoch - Scheduling next epoch one interval into the future";

			// Lagged too much - reset epoch interval
			_next_epoch_time = now + EPOCH_INTERVAL;
		}
	}
}

void FlowControl::OnLoss(u32 bytes)
{

}
