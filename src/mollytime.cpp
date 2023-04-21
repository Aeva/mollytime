
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

#include <fmt/format.h>

#include <chrono>
#include <string>
#include <vector>
#include <iostream>


using Clock = std::chrono::high_resolution_clock;


enum class StatusCode
{
	PASS,
	FAIL
};

#define FAILED(Expr) (Expr == StatusCode::FAIL)
#define RETURN_ON_FAIL(Expr) { StatusCode Result = Expr; if (Result == StatusCode::FAIL) return Result; }


SDL_Window* Window = nullptr;
SDL_GLContext Context = nullptr;

snd_seq_t *SeqHandle = nullptr;


struct ExtPort
{
	int Client;
	int Port;
	unsigned int Cap;
	unsigned int Type;
	std::string Name;
	bool Available;
};


std::vector<ExtPort> AllDevices;
std::vector<ExtPort> ExtInputs;
std::vector<ExtPort> ExtOutputs;


void RefreshDeviceList()
{
	AllDevices.clear();
	ExtInputs.clear();
	ExtOutputs.clear();

	snd_seq_client_info_t* ClientInfo = nullptr;
	snd_seq_client_info_alloca(&ClientInfo);

	snd_seq_port_info_t* PortInfo = nullptr;
	snd_seq_port_info_alloca(&PortInfo);

	snd_seq_client_info_set_client(ClientInfo, -1);
	while (snd_seq_query_next_client(SeqHandle, ClientInfo) >= 0)
	{
		const char* ClientName = snd_seq_client_info_get_name(ClientInfo);
		int Client = snd_seq_client_info_get_client(ClientInfo);
		snd_seq_port_info_set_client(PortInfo, Client);
		snd_seq_port_info_set_port(PortInfo, -1);
		while (snd_seq_query_next_port(SeqHandle, PortInfo) >= 0)
		{
			unsigned int PortCap = snd_seq_port_info_get_capability(PortInfo);
			const bool Read = (PortCap & SND_SEQ_PORT_CAP_READ) != 0;
			const bool Write = (PortCap & SND_SEQ_PORT_CAP_WRITE) != 0;

			unsigned int PortType = snd_seq_port_info_get_type(PortInfo);
			const bool Generic = (PortType & SND_SEQ_PORT_TYPE_MIDI_GENERIC) != 0;

			const char* PortName = snd_seq_port_info_get_name(PortInfo);
			std::string DeviceName = fmt::format("{} : {}", ClientName, PortName);
			int Port = snd_seq_port_info_get_port(PortInfo);

			ExtPort Device = { Client, Port, PortCap, PortType, DeviceName, false };

			if (Generic && (Read || Write))
			{
				Device.Available = true;
				if (Read)
				{
					ExtInputs.push_back(Device);
				}
				if (Write)
				{
					ExtOutputs.push_back(Device);
				}
			}

			AllDevices.push_back(Device);
		}
	}
}


bool ShowDemoWindow = false;
bool ShowConnections = false;
bool ShowDeviceDebug = false;
bool ShowFrameRate = false;

float PresentFrequency = 0.0;
float PresentDeltaMs = 0.0;

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

		Clock::time_point FrameStartTimePoint = Clock::now();
		double CurrentTime;
		{
			static Clock::time_point StartTimePoint = FrameStartTimePoint;
			static Clock::time_point LastTimePoint = StartTimePoint;
			{
				std::chrono::duration<double, std::milli> FrameDelta = FrameStartTimePoint - LastTimePoint;
				PresentDeltaMs = float(FrameDelta.count());
			}
			{
				std::chrono::duration<double, std::milli> EpochDelta = FrameStartTimePoint - StartTimePoint;
				CurrentTime = float(EpochDelta.count());
			}
			LastTimePoint = FrameStartTimePoint;
			PresentFrequency = float(1000.0 / PresentDeltaMs);
		}

		ImGui_ImplOpenGL2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit"))
				{
					Live = false;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Settings"))
			{
				if (ImGui::MenuItem("MIDI Ports", nullptr, &ShowConnections))
				{
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Debug"))
			{
				if (ImGui::MenuItem("Show FPS", nullptr, &ShowFrameRate))
				{
				}
				if (ImGui::MenuItem("ALSA Devices", nullptr, &ShowDeviceDebug))
				{
				}
				if (ImGui::MenuItem("Dear ImGui", nullptr, &ShowDemoWindow))
				{
				}
				ImGui::EndMenu();
			}

			if (ShowFrameRate)
			{
				ImGui::Text(" %.0f hz\n", round(PresentFrequency));
			}

			ImGui::EndMainMenuBar();
		}

		if (ShowDemoWindow)
		{
			ImGui::ShowDemoWindow(&ShowDemoWindow);
		}

		if (ShowDeviceDebug || ShowConnections)
		{
			RefreshDeviceList();
		}

		if (ShowDeviceDebug)
		{
			int Margin = 0;
			const ImGuiViewport* MainViewport = ImGui::GetMainViewport();
			ImGui::SetNextWindowPos(ImVec2(MainViewport->WorkPos.x + Margin, MainViewport->WorkPos.y + Margin), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(MainViewport->WorkSize.x - Margin * 2, MainViewport->WorkSize.y - Margin * 2), ImGuiCond_Always);

			ImGuiWindowFlags WindowFlags = \
				ImGuiWindowFlags_AlwaysVerticalScrollbar |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoResize |
				ImGuiWindowFlags_NoMove;

			if (ImGui::Begin("ALSA Device Information", &ShowDeviceDebug, WindowFlags))
			{
				ImGui::SeparatorText("MIDI Devices");
				int i = 0;
				for (const ExtPort& Device : AllDevices)
				{
					ImGui::PushID(i++);
					if (Device.Available)
					{
						ImGui::TextColored(ImVec4(0.0, 1.0, 0.0, 1.0), Device.Name.c_str());
					}
					else
					{
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5, 0.5, 0.5, 1.0));
						ImGui::Text(Device.Name.c_str());
					}

#define PORT_CAP(CAP) \
					if (Device.Cap & CAP) \
					{ \
						ImGui::BulletText(#CAP); \
					}

					PORT_CAP(SND_SEQ_PORT_CAP_READ);
					PORT_CAP(SND_SEQ_PORT_CAP_WRITE);
					PORT_CAP(SND_SEQ_PORT_CAP_SYNC_READ);
					PORT_CAP(SND_SEQ_PORT_CAP_SYNC_WRITE);
					PORT_CAP(SND_SEQ_PORT_CAP_DUPLEX);
					PORT_CAP(SND_SEQ_PORT_CAP_SUBS_READ);
					PORT_CAP(SND_SEQ_PORT_CAP_SUBS_WRITE);
					PORT_CAP(SND_SEQ_PORT_CAP_NO_EXPORT);
#undef PORT_CAP

#define PORT_TYPE(TYPE) \
					if (Device.Type & TYPE) \
					{ \
						ImGui::BulletText(#TYPE); \
					}

					PORT_TYPE(SND_SEQ_PORT_TYPE_SPECIFIC);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_GENERIC);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_GM);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_GS);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_XG);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_MT32);
					PORT_TYPE(SND_SEQ_PORT_TYPE_MIDI_GM2);
					PORT_TYPE(SND_SEQ_PORT_TYPE_SYNTH);
					PORT_TYPE(SND_SEQ_PORT_TYPE_DIRECT_SAMPLE);
					PORT_TYPE(SND_SEQ_PORT_TYPE_SAMPLE);
					PORT_TYPE(SND_SEQ_PORT_TYPE_HARDWARE);
					PORT_TYPE(SND_SEQ_PORT_TYPE_SOFTWARE);
					PORT_TYPE(SND_SEQ_PORT_TYPE_SYNTHESIZER);
					PORT_TYPE(SND_SEQ_PORT_TYPE_PORT);
					PORT_TYPE(SND_SEQ_PORT_TYPE_APPLICATION);
#undef PORT_TYPE

					if (!Device.Available)
					{
						ImGui::PopStyleColor(1);
					}
					ImGui::PopID();
				}
			}
			ImGui::End();
		}

		if (ShowConnections)
		{
			ImGuiWindowFlags WindowFlags = 0;
			if (ImGui::Begin("MIDI Device Selection", &ShowConnections, WindowFlags))
			{
				static int SelectedInput = 0;
				static int SelectedOutput = 0;

				if (SelectedInput >= ExtInputs.size())
				{
					SelectedInput = 0;
				}

				if (SelectedOutput >= ExtOutputs.size())
				{
					SelectedOutput = 0;
				}

				ImGui::SeparatorText("Input Device");
				{
					ImGui::PushID("MIDI input selection");
					for (int i = 0; i < ExtInputs.size(); ++i)
					{
						ImGui::PushID(i);
						if (ImGui::RadioButton(ExtInputs[i].Name.c_str(), SelectedInput == i))
						{
							SelectedInput = i;
						}
						ImGui::PopID();
					}
					ImGui::PopID();
				}

				ImGui::SeparatorText("Output Device");
				{
					ImGui::PushID("MIDI output selection");
					for (int i = 0; i < ExtOutputs.size(); ++i)
					{
						ImGui::PushID(i);
						if (ImGui::RadioButton(ExtOutputs[i].Name.c_str(), SelectedOutput == i))
						{
							SelectedOutput = i;
						}
						ImGui::PopID();
					}
					ImGui::PopID();
				}
			}
			ImGui::End();
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
	{
		std::cout << "Creating ALSA client... ";
		if (snd_seq_open(&SeqHandle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) == 0)
		{
			snd_seq_set_client_name(SeqHandle, "mollytime");
			std::cout << "Done!\n";
		}
		else
		{
			std::cout << "Failed to initialize ALSA.\n";
			return StatusCode::FAIL;
		}
	}


	std::cout << "Using device: " << glGetString(GL_RENDERER) << " " << glGetString(GL_VERSION) << "\n";
	return StatusCode::PASS;
}


void Teardown()
{
	if (SeqHandle)
	{
		snd_seq_close(SeqHandle);
	}
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
