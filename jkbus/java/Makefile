JAVA_HOME ?=/usr/lib/jvm/default-java/
JAVAC=javac

SRCS=Ksock.java Message.java MessageId.java KsockException.java OriginallyFrom.java Reply.java Request.java
LOCATED_SRCS=$(SRCS:%=src/%)
CLASSES=$(SRCS:%.java=classes/%.class)

TEST_LIBS_PATH=../native/lib:../../libkbus/

.PHONY: all
all: dirs classes
	cd classes && jar cf ../jar/Kbus.jar *

.PHONY: jni
jni:
	javah -classpath classes/ -d include/ -jni com.kynesim.kbus.Ksock com.kynesim.kbus.Message

.PHONY: classes
classes: $(LOCATED_SRCS)
	$(JAVAC) -d classes $(LOCATED_SRCS)

.PHONY: clean
clean:
	rm -rf jar
	rm -rf classes
	rm -rf docs
	rm -rf include

.PHONY: dirs
dirs:
	mkdir -p src
	mkdir -p jar
	mkdir -p classes

.PHONY: docs
docs: $(LOCATED_SRCS)
	mkdir -p docs
	javadoc -d docs  $(LOCATED_SRCS)

.PHONY: jtest
jtest: test/JKbusTest.java
	javac -cp `pwd`/jar/Kbus.jar:. test/JKbusTest.java

.PHONY: runtest
runtest: jtest native
	LD_LIBRARY_PATH=$(TEST_LIBS_PATH) java -cp `pwd`/jar/Kbus.jar:./test/  JKbusTest

# Slightly icky - we need the native library to be built...
.PHONY: native
native:
	make -C ../native
