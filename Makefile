SHELL = /bin/bash
PGLIB = $(shell echo `pg_config --pkglibdir`)/
GCC = g++ --std=c++11
all: clean
	$(GCC) smtp.cpp -L$(PGLIB) -lpthread -lpq -o smtp
clean:
	rm -f smtp smtp.o

# apt-get install libpq-dev
