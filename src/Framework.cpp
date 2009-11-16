/*
    Copyright 2009 Christopher A. Taylor

    This file is part of LibCat.

    LibCat is free software: you can redistribute it and/or modify
    it under the terms of the Lesser GNU General Public License as
    published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.

    LibCat is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    Lesser GNU General Public License for more details.

    You should have received a copy of the Lesser GNU General Public
    License along with LibCat.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cat/AllFramework.hpp>
using namespace cat;

// Framework Initialize
void InitializeFramework()
{
	// Initialize custom memory allocator subsystem
	if (!RegionAllocator::ref()->Valid())
	{
		FatalStop("Custom memory allocator failed to initialize");
	}

	// Initialize logging subsystem with INANE reporting level
	Logging::ref()->Initialize(LVL_INANE);

	// Initialize disk settings subsystem
	Settings::ref()->read();

	// Read logging subsystem settings
	Logging::ref()->ReadSettings();

	// Start the worker threads
	ThreadPool::ref()->Startup();
}

// Framework Shutdown
void ShutdownFramework(bool WriteSettings)
{
	// Terminate worker threads
	ThreadPool::ref()->Shutdown();

	// Write settings to disk if requested
	if (WriteSettings)
		Settings::ref()->write();

	// Shut down logging thread
	Logging::ref()->Shutdown();
}
