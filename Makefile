# these are pretty much the only things that need to be configured
MONO_SOURCE?=/opt/src/mono/mono
MANDROID_ROOT=/Developer/MonoAndroid
ANDROID_API_LEVEL=8



ifeq (/Developer/MonoAndroid,$(shell cd /Developer/MonoAndroid && pwd))
MANDROID_INSTALLED=1
endif

LIBMONODROID?=/data/data/$(APPID)/lib/libmonodroid.so
ANDROID_DUMP_PATH?=/data/data/$(APPID)/cache


all:: all-osx

ifeq (1,$(MANDROID_INSTALLED))
all:: check-appid all-android
endif


all-osx: MonoGCPoker-osx.dll libpoker.dylib test.exe
all-android: check-appid MonoGCPoker-android.dll libpoker.so

check-appid:
	@if test -z "$(APPID)"; then echo "*** error: you have to define the APPID variable when building mono for android support, e.g. APPID=com.widget.widgetapp"; exit 1; fi

test:
	DYLD_FALLBACK_LIBRARY_PATH=. mono --gc=sgen test.exe
	dot -Tsvg heap-graph.dot > heap-graph.svg
	@echo open $(shell pwd)/heap-graph.svg in your web browser

MonoGCPoker-android.cs: check-appid MonoGCPoker.cs.in
	sed -e "s,@ANDROID_DUMP_PATH@,$(ANDROID_DUMP_PATH)," < $< > $@

MonoGCPoker-android.dll: check-appid MonoGCPoker-android.cs
	$(MANDROID_ROOT)/usr/bin/smcs -debug -target:library -define:ANDROID -out:$@ $< -r:$(MANDROID_ROOT)/usr/lib/mandroid/platforms/android-$(ANDROID_API_LEVEL)/Mono.Android.dll

MonoGCPoker-osx.dll: MonoGCPoker.cs.in
	gmcs -debug -target:library -out:$@ $<

test.exe:  test.cs MonoGCPoker-osx.dll
	gmcs -debug -target:exe -out:$@ $< -r:MonoGCPoker-osx.dll

clean:
	rm -f *.dll *.exe *.mdb *.o *.dylib *.so MonoGCPoker-android.cs

COMMON_CFLAGS=-g -I$(MONO_SOURCE) -I$(MONO_SOURCE)/eglib/src
OSX_CC=gcc
OSX_CFLAGS=-arch i386 $(COMMON_CFLAGS)
%.o.osx: %.c
	$(OSX_CC) $(OSX_CFLAGS) -c $< -o $@

libpoker.dylib: poker.o.osx
	$(OSX_CC) -g -arch i386 -dynamiclib -undefined suppress -flat_namespace -o libpoker.dylib poker.o.osx

ANDROID_ROOT=/opt/android-$(ANDROID_API_LEVEL)
ANDROID_BIN=$(ANDROID_ROOT)/bin
ANDROID_CC=arm-linux-androideabi-gcc
ANDROID_CFLAGS=-DANDROID=1 $(COMMON_CFLAGS) -DLIBMONODROID=$(LIBMONODROID)

libpoker.so: poker.o.android
	PATH=$(ANDROID_BIN):$$PATH $(ANDROID_CC) $(ANDROID_LDFLAGS) -g -shared poker.o.android -o libpoker.so -llog

%.o.android: %.c
	PATH=$(ANDROID_BIN):$$PATH $(ANDROID_CC) $(ANDROID_CFLAGS) -c $< -o $@
