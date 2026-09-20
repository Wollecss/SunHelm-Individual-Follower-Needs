#pragma once

#include "SKSEMenuFramework.h"

namespace UI
{
	void Register();

	namespace Status
	{
		void __stdcall Render();
	}

	namespace Config
	{
		void __stdcall Render();
	}
}
