#include <cat/net/FlowControl.hpp>
using namespace cat;

FlowControl::FlowControl()
{
	_max_epoch_bytes = MIN_RATE_LIMIT;
}
