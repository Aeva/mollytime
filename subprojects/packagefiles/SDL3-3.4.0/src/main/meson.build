if host_machine.system() == 'windows'
  subdir('windows')
  subdir('generic')
elif host_machine.system() == 'emscripten'
  subdir('emscripten')
  subdir('generic')
else
  subdir('generic')
endif

sources += files('SDL_main_callbacks.c', 'SDL_runapp.c')
