#include "3-calc.h"
#include <stdlib.h>

/**
 * get_op_func - selects the function matching the operator
 * @s: the operator passed as argument to the program
 *
 * Return: a pointer to the matching function, or NULL if none matches
 */
int (*get_op_func(char *s))(int, int)
{
	op_t ops[] = {
		{"+", op_add},
		{"-", op_sub},
		{"*", op_mul},
		{"/", op_div},
		{"%", op_mod},
		{NULL, NULL}
	};
	int i;

	i = 0;
	while (ops[i].op != NULL && (*(ops[i].op) != *s || s[1] != '\0'))
	{
		i++;
	}
	return (ops[i].f);
}
