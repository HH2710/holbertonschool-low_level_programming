#include "main.h"

/**
 * _atoi - converts a string to an integer
 * @s: the string to convert
 *
 * Return: the converted integer
 */
int _atoi(char *s)
{
	int i = 0;
	int sign = 1;
	int found = 0;
	int result = 0;

	while (s[i] != '\0')
	{
		if (s[i] == '-')
			sign = -sign;
		else if (s[i] >= '0' && s[i] <= '9')
		{
			found = 1;
			result = result * 10 + (s[i] - '0');
		}
		else if (found)
			break;
		i++;
	}

	return (result * sign);
}
