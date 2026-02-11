if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'emscripten'
  subdir('emscripten')
else
  subdir('unix')
endif

sources += files('SDL_url.c')
