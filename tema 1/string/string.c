// SPDX-License-Identifier: BSD-3-Clause

#include <string.h>
#include <stdlib.h>
#include <internal/types.h>

char *strcpy(char *destination, const char *source)
{
	char *_destination = destination;
	const char *_source = source;

	if (_destination && _source)
		while (*_source)
			*(_destination++) = *(_source++);

	*_destination = '\0';

	return destination;
}

char *strncpy(char *destination, const char *source, size_t len)
{
	char *_destination = destination;
	const char *_source = source;

	if (_destination && _source) {
		size_t pos = 0;

		for (pos = 0; pos < len && *_source; pos++)
			*(_destination++) = *(_source++);

		for (; pos < len; pos++) {
			*(_destination++) = '\0';
		}
	}

	return destination;
}

char *strcat(char *destination, const char *source)
{
	char *_destination = destination;
	const char *_source = source;

	if (_destination)
		while (*_destination)
			_destination++;

	if (_destination && _source)
		while (*_source)
			*(_destination++) = *(_source++);

	*_destination = '\0';

	return destination;
}

char *strncat(char *destination, const char *source, size_t len)
{
	char *_destination = destination;
	const char *_source = source;

	if (_destination)
		while (*_destination)
			_destination++;

	if (_destination && _source) {
		for (size_t pos = 0; pos < len && *_source; pos++)
			*(_destination++) = *(_source++);
	}

	*_destination = '\0';

	return destination;
}

int strcmp(const char *str1, const char *str2)
{
	const char *_str1 = str1;
	const char *_str2 = str2;

	if (str1 && str2)
		while (*_str1 && *_str2) {
			if (*_str1 != *_str2)
				return (*_str1) - (*_str2);

			_str1++;
			_str2++;
		}

	return (*_str1) - (*_str2);
}

int strncmp(const char *str1, const char *str2, size_t len)
{
	const char *_str1 = str1;
	const char *_str2 = str2;

	if (str1 && str2) {
		for (size_t pos = 1; pos < len && *_str1 && *_str2; pos++) {
			if (*_str1 != *_str2)
				return (*_str1) - (*_str2);

			_str1++;
			_str2++;
		}
	}

	return (*_str1) - (*_str2);
}

size_t strlen(const char *str)
{
	size_t i = 0;

	for (; *str != '\0'; str++, i++)
		;

	return i;
}

char *strchr(const char *str, int c)
{
	const char *_str = str;

	if (_str)
		while (*_str && *_str != c)
			_str++;

	return (char *)((*_str == c) ? _str : NULL);
}

char *strrchr(const char *str, int c)
{
	char *_str = (char *)str;
	char *ret = NULL;

	if (_str)
		while (*_str) {
			if (*_str == c)
				ret = _str;
			_str++;
		}

	return ret;
}

// KMP algorithm running in O(strlen(haystack) + strlen(needle))
char *strstr(const char *haystack, const char *needle)
{
	const char *_haystack = haystack;
	size_t len = (int)strlen(needle);
	int *pf = (int *)malloc((len + 1) * sizeof(char));
	size_t i, j;

	for (i = 1, j = 0; needle[i]; i++) {
		while (needle[i] != needle[j] && j)
			j = pf[j - 1];

		if (needle[i] == needle[j])
			pf[i] = (j++) + 1;
	}

	for (i = j = 0; _haystack[i]; i++) {
		while (_haystack[i] != needle[j] && j)
			j = pf[j - 1];

		if (_haystack[i] == needle[j])
			j++;

		if (j == len) {
			free(pf);
			return (char *)(haystack + i - len + 1);
		}
	}

	free(pf);

	return NULL;
}

char *strrstr(const char *haystack, const char *needle)
{
	size_t strlen_haystack = strlen(haystack);
	size_t strlen_needle = strlen(needle);

	for (char *rpos = (char *)haystack + strlen_haystack - strlen_needle; rpos != haystack; rpos--)
		if (!strncmp(rpos, needle, strlen_needle))
			return rpos;

	return NULL;
}

void *memcpy(void *destination, const void *source, size_t num)
{
	char *_destination = (char *)destination;
	const char *_source = (const char *)source;

	if (_destination && source) {
		for (size_t pos = 0; pos < num; pos++) {
			*(_destination++) = *(_source++);
		}
	}

	return destination;
}

void *memmove(void *destination, const void *source, size_t num)
{
	char *_destination = (char *)destination;
	const char *_source = (const char *)source;
	char *temp = malloc(num * sizeof(char));
	char *_temp = temp;

	if (_destination && source) {
		for (size_t pos = 0; pos < num; pos++) {
			*(_temp++) = *(_source++);
		}

		_temp = temp;
		for (size_t pos = 0; pos < num; pos++) {
			*(_destination++) = *(_temp++);
		}
	}

	free(temp);

	return destination;
}

int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
	const unsigned char *_ptr1 = (unsigned char *)ptr1;
	const unsigned char *_ptr2 = (unsigned char *)ptr2;

	if (_ptr1 && _ptr2) {
		for (size_t pos = 0; pos < num; pos++) {
			if (*_ptr1 != *_ptr2)
				return (*_ptr1) - (*_ptr2);

			_ptr1++;
			_ptr2++;
		}
	}

	return 0;
}

void *memset(void *source, int value, size_t num)
{
	char *_source = (char *)source;

	if (source) {
		for (size_t pos = 0; pos < num; pos++)
			*(uint8_t *)(_source++) = value;
	}

	return source;
}
