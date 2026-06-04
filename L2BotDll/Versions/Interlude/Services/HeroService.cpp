#include "pch.h"
#include "HeroService.h"

namespace Interlude
{
	std::mutex g_CommandMutex;
	std::vector<std::function<void()>> g_CommandQueue;
}
