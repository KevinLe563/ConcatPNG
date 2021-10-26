#pragma once

#include "crc.h"

char *parse_chunk(FILE* file, int *length, char *type);
