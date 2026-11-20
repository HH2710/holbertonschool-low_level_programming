#include "main.h"
#include <stdlib.h>

/**
 * array_range - creates an array of integers from min to max
 * @min: first value
 * @max: last value
 *
 * Return: pointer to the new array, or NULL on failure
 */
int *array_range(int min, int max)
{
	int *tab;
	int i;
	int taille;

	if (min > max)
		return (NULL);

	taille = max - min + 1;

	tab = malloc(sizeof(int) * taille);
	if (tab == NULL)
		return (NULL);

	for (i = 0; i < taille; i++)
	{
		tab[i] = min + i;
	}

	return (tab);
}
