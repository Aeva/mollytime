if host_machine.system() == 'emscripten'
  cdata.set('SDL_CAMERA_DRIVER_EMSCRIPTEN', 1))
  subdir('emscripten')
else
  subdir('dummy')
endif

sources += files('SDL_camera.c')
