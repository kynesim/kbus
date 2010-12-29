JAVA_HOME ?=/usr/lib/jvm/default-java/
JAVAC=javac
O ?= .
VER=0.1

SRCS=Ksock.java KMessage.java KMessageId.java KsockException.java KOriginallyFrom.java KReply.java KRequest.java
LOCATED_SRCS=$(SRCS:%=src/%)
CLASSES=$(SRCS:%.java=classes/%.class)

TEST_LIBS_PATH=$(O)/lib:$(O)/libkbus/

.PHONY: all
all: dirs classes jni
	cd $(O)/classes && jar cf $(O)/Kbus-$(VER).jar *

.PHONY: jni
jni:
	javah -classpath $(O)/classes/ -d $(O)/include/ -jni com.kynesim.kbus.Ksock com.kynesim.kbus.KMessage

.PHONY: classes
classes: $(LOCATED_SRCS)
	$(JAVAC) -d $(O)/classes $(LOCATED_SRCS)

.PHONY: clean
clean:
	rm -rf $(O)/jar
	rm -rf $(O)/classes
	rm -rf $(O)/docs
	rm -rf $(O)/include

.PHONY: dirs
dirs:
	mkdir -p $(O)/jar
	mkdir -p $(O)/classes

.PHONY: docs
docs: $(LOCATED_SRCS)
	mkdir -p $(O)/docs
	javadoc -d $(O)/docs  $(LOCATED_SRCS)

.PHONY: jtest
jtest: test/JKbusTest.java
	javac -d $(O) -cp $(O)/Kbus-$(VER).jar:. test/JKbusTest.java

.PHONY: runtest
runtest: jtest native
	LD_LIBRARY_PATH=$(TEST_LIBS_PATH) java -cp `pwd`/jar/Kbus-$(VER).jar:./test/  JKbusTest

# Slightly icky - we need the native library to be built...
.PHONY: native
native:
	make -C ../native
