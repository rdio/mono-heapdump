
LIBMONO?=/Library/Frameworks/Mono.framework/Libraries/libmonosgen-2.0.0.dylib
MONO_SOURCE?=/opt/src/mono/mono
ANDROID_DUMP_PATH?=/data/data/<my-app>/cache/heap-graph.dot

all: MonoGCPoker-android.dll MonoGCPoker-osx.dll libpoker.dylib test.exe

test:
	DYLD_FALLBACK_LIBRARY_PATH=. mono --gc=sgen test.exe
	dot -Tsvg heap-graph.dot > heap-graph.svg
	@echo open $(shell pwd)/heap-graph.svg in your web browser

MonoGCPoker-android.cs: MonoGCPoker.cs.in
	sed -e "s,@ANDROID_DUMP_PATH@,$(ANDROID_DUMP_PATH)," < $< > $@

MonoGCPoker-android.dll: MonoGCPoker-android.cs
	gmcs -debug -target:library -define:ANDROID -out:$@ $<

MonoGCPoker-osx.dll: MonoGCPoker.cs.in
	gmcs -debug -target:library -out:$@ $<

test.exe:  test.cs MonoGCPoker-osx.dll
	gmcs -debug -target:exe -out:$@ $< -r:MonoGCPoker-osx.dll

clean:
	rm -f *.dll *.exe *.mdb *.o *.dylib *.so MonoGCPoker-android.cs

COMMON_CFLAGS=-g -I$(MONO_SOURCE) -DLIBMONO=$(LIBMONO)
OSX_CC=gcc
OSX_CFLAGS=-arch i386 $(COMMON_CFLAGS)
%.o.osx: %.c
	$(OSX_CC) $(OSX_CFLAGS) -c $< -o $@

libpoker.dylib: poker.o.osx
	$(OSX_CC) -g -arch i386 -dynamiclib -undefined suppress -flat_namespace -o libpoker.dylib poker.o.osx

ANDROID_ROOT=/opt/android-8
ANDROID_BIN=$(ANDROID_ROOT)/bin
ANDROID_CC=arm-linux-androideabi-gcc
ANDROID_CFLAGS=-DANDROID=1 $(COMMON_CFLAGS)

libpoker.so: poker.o.android
	PATH=$(ANDROID_BIN):$$PATH $(ANDROID_CC) $(ANDROID_LDFLAGS) -g -shared poker.o.android -o libpoker.so -llog

%.o.android: %.c
	PATH=$(ANDROID_BIN):$$PATH $(ANDROID_CC) $(ANDROID_CFLAGS) -c $< -o $@
