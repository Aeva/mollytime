#subdir('generic')

if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'emscripten'
  subdir('generic')
else
  subdir('pthread')
endif

sources += files('SDL_thread.c')
