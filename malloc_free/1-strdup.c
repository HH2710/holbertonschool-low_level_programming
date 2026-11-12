#include "main.h"
#include <stdlib.h>

/**
 * _strdup - returns a pointer to a copy of a string
 * @str: the string to duplicate
 *
 * Return: pointer to the new string, or NULL if str is NULL
 * or if malloc fails
 */
char *_strdup(char *str)
{
	char *dup;
	unsigned int len, i;

	if (str == NULL)
		return (NULL);

	len = 0;
	while (str[len] != '\0')
		len++;

	dup = malloc(sizeof(char) * (len + 1));
	if (dup == NULL)
		return (NULL);

	for (i = 0; i <= len; i++)
		dup[i] = str[i];

	return (dup);
}
