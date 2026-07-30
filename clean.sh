rm -f config.mk config.h config.log scummvm && echo 'Removed config files and binary'
find . -name '*.o' -delete && echo 'Removed all .o files'
find . -name '*.d' -delete && echo 'Removed all .d dependency files'