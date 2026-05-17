#include "stdafx.h"
#include "RayTracerDemo.h"

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	RayTracerDemo rtdemo(1280, 720, L"Ray Tracer Demo");
	return Win32Application::Run(&rtdemo, hInstance, nCmdShow);
}
