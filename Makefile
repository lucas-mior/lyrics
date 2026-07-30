CC ?= cc
PYTHON ?= python3
ONNXRUNTIME_ROOT ?= /usr/include/onnxruntime

CPPFLAGS += -I$(ONNXRUNTIME_ROOT)
CFLAGS += -std=c11 -O2 -g -Wall -Wextra -Werror
LDLIBS += -lonnxruntime -lm

PROGRAM := bin/ort-identity
SOURCE := src/main.c
MODEL := models/identity.onnx

.PHONY: all clean model run setup

all: $(PROGRAM) $(MODEL)

$(PROGRAM): $(SOURCE)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCE) $(LDFLAGS) $(LDLIBS) -o $@

$(MODEL): scripts/create_identity_model.py
	$(PYTHON) $< $@

model: $(MODEL)

setup:
	./scripts/get_onnxruntime.sh
	$(MAKE) model

run: all
	LD_LIBRARY_PATH="$(ONNXRUNTIME_ROOT)/lib$${LD_LIBRARY_PATH:+:$$LD_LIBRARY_PATH}" \
		./$(PROGRAM) $(MODEL)

clean:
	rm -rf bin $(MODEL)
