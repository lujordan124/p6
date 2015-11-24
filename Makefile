# Jordan Lu (jjl4mb)
# 11/23/2015
# Makefile

#Virtual Memory was implemented in C++
CXX	= g++


main:	myApp.cpp
	$(CXX) -w myApp.cpp fileSystem.cpp disk.cpp

clean:
	/bin/rm -f *.o *~
