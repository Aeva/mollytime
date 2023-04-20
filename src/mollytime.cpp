
#include <alsa/asoundlib.h>
#include <GL/gl.h>

#ifdef MINIMAL_DLL
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_clipboard.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_opengl2.h>

#include <iostream>


enum class StatusCode
{
	PASS,
	FAIL
};

#define FAILED(Expr) (Expr == StatusCode::FAIL)
#define RETURN_ON_FAIL(Expr) { StatusCode Result = Expr; if (Result == StatusCode::FAIL) return Result; }


SDL_Window* Window = nullptr;
SDL_GLContext Context = nullptr;


void MainLoop()
{
	bool Live = true;

	while (Live)
	{
		SDL_Event Event;

		int ScreenWidth;
		int ScreenHeight;
		SDL_GetWindowSize(Window, &ScreenWidth, &ScreenHeight);

		while (SDL_PollEvent(&Event))
		{
			ImGui_ImplSDL2_ProcessEvent(&Event);
			if (Event.type == SDL_QUIT ||
				(Event.type == SDL_WINDOWEVENT && Event.window.event == SDL_WINDOWEVENT_CLOSE && Event.window.windowID == SDL_GetWindowID(Window)))
			{
				Live = false;
				break;
			}
		}

		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		{
			bool ShowDemoWindow = true;
			ImGui::ShowDemoWindow(&ShowDemoWindow);
		}

		ImGui::Render();
		glViewport(0, 0, ScreenHeight, ScreenWidth);
		glClearColor(0.25, 0.0, .5, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
		SDL_GL_SwapWindow(Window);
	}
}


StatusCode Boot()
{
	{
		std::cout << "Setting up SDL2... ";
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) == 0)
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#if ENABLE_DEBUG_CONTEXTS
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
			Uint32 WindowFlags = \
				SDL_WINDOW_OPENGL |
				SDL_WINDOW_RESIZABLE;

			SDL_DisplayMode DisplayMode;
			SDL_GetCurrentDisplayMode(0, &DisplayMode);
			int WindowWidth = int(float(DisplayMode.w) * .9);
			int WindowHeight = int(float(DisplayMode.h) * .9);

			Window = SDL_CreateWindow(
				"mollytime",
				SDL_WINDOWPOS_CENTERED,
				SDL_WINDOWPOS_CENTERED,
				WindowWidth, WindowHeight,
				WindowFlags);
		}
		else
		{
			std::cout << "Failed to initialize SDL2.\n";
			return StatusCode::FAIL;
		}
		if (Window == nullptr)
		{
			std::cout << "Failed to create SDL2 window.\n";
			return StatusCode::FAIL;
		}
		else
		{
			Context = SDL_GL_CreateContext(Window);
			if (Context == nullptr)
			{
				std::cout << "Failed to create SDL2 OpenGL Context.\n";
				return StatusCode::FAIL;
			}
			else
			{
				SDL_GL_MakeCurrent(Window, Context);
				SDL_GL_SetSwapInterval(1);
				std::cout << "Done!\n";
			}
		}
	}
	// ConnectDebugCallback(0);
	{
		std::cout << "Setting up Dear ImGui... ";
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

		ImGui::StyleColorsDark();
		ImGuiStyle& Style = ImGui::GetStyle();
		Style.FrameBorderSize = 1.0f;
		ImGui_ImplSDL2_InitForOpenGL(Window, Context);
		ImGui_ImplOpenGL2_Init();
		std::cout << "Done!\n";
	}
	std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";
	return StatusCode::PASS;
}


void Teardown()
{
	if (Context)
	{
		ImGui_ImplOpenGL2_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		SDL_GL_DeleteContext(Context);
	}
	if (Window)
	{
		SDL_DestroyWindow(Window);
	}
	SDL_Quit();
}


int main(int argc, char* argv[])
{
	if (Boot() == StatusCode::PASS)
	{
		MainLoop();
	}
	Teardown();
}
