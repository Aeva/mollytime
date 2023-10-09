//

#include <glad/gl.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_clipboard.h>

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>
#include <RmlUi_Backend.h>

#include <fmt/format.h>

#include <string>


int main(int ArgCount, char* ArgValues[])
{
	int WindowWidth = 1024;
	int WindowHeight = 1024;

	if (!Backend::Initialize("MollyTime", WindowWidth, WindowHeight, true))
	{
		fmt::print("Failed to initialize the RmlUi backend.\n");
		return 1;
	}

	Rml::SetSystemInterface(Backend::GetSystemInterface());
	Rml::SetRenderInterface(Backend::GetRenderInterface());
	Rml::Initialise();

	Rml::Context* RmlUiContext = Rml::CreateContext("MollyTime", Rml::Vector2i(WindowWidth, WindowHeight));
	if (!RmlUiContext)
	{
		fmt::print("Failed to create a RmlUi context.\n");
		Rml::Shutdown();
		Backend::Shutdown();
		return 1;
	}

	Rml::LoadFontFace("C:\\Windows\\Fonts\\segoeui.ttf");

	Rml::Debugger::Initialise(RmlUiContext);

	{
		const std::string Source = ""
			"<rml><head>\n"
			"<style>\n"
			"body\n"
			"{\n"
			"	font-family: Segoe UI;\n"
			"	font-weight: normal;\n"
			"	font-style: normal;\n"
			"	font-size: 32pt;\n"
			"	color: white;\n"
			"}\n"
			"h1\n"
			"{\n"
				"font-size: 64pt;\n"
				"font-weight: bold;\n"
			"}\n"
			"</style>\n"
			"</head><body>\n"
			"	<h1>Hail Eris!</h1><br/>\n"
			"	Hark!  The quick brown fox jumps over the lazy dog!\n"
			"	<br/>The flesh is weak, but the spirit is willing.\n"
			"</body></rml>\n";
		
		if (Rml::ElementDocument* Document = RmlUiContext->LoadDocumentFromMemory(Source))
		{
			Document->Show();
		}
	}

	bool Live = true;
	while (Live)
	{
		Live = Backend::ProcessEvents(RmlUiContext, nullptr, true);

		RmlUiContext->Update();

		Backend::BeginFrame();
		RmlUiContext->Render();
		Backend::PresentFrame();
	}

	Rml::Shutdown();
	Backend::Shutdown();
	return 0;
}
