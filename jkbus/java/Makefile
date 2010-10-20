JAVA_HOME ?=/usr/lib/jvm/default-java/
JAVAC=javac

SRCS=Ksock.java
LOCATED_SRCS=$(SRCS:%=src/%)
CLASSES=$(SRCS:%.java=classes/%.class)

all: dirs $(CLASSES) 
	cd classes && jar cf ../jar/Kbus.jar *

jni:
	javah -classpath classes/ -d include/ -jni com.kynesim.kbus.Ksock

classes/%.class: src/%.java
	$(JAVAC) -d classes $^ 

make clean:
	rm -rf jar
	rm -rf classes
	rm -rf docs
	rm -rf include

dirs:
	mkdir -p src
	mkdir -p jar
	mkdir -p classes

docs: $(LOCATED_SRCS)
	mkdir -p docs
	javadoc -d docs  $(LOCATED_SRCS)

test:
#	

runtest:
	java -cp `pwd`/jar/Kbus.jar:.  KbusTest