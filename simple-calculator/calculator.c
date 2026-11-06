#include <stdio.h>

int main(void)
{
    int choice;
    int a, b, result;

    printf("Simple Calculator\n");

    do
    {
        printf("1) Add\n");
        printf("2) Subtract\n");
        printf("3) Multiply\n");
        printf("4) Divide\n");
        printf("0) Quit\n");
        printf("Choice: ");

        if (scanf("%d", &choice) != 1)
            return (1);

        if (choice == 1 || choice == 2)
        {
            printf("A: ");
            if (scanf("%d", &a) != 1)
                return (1);

            printf("B: ");
            if (scanf("%d", &b) != 1)
                return (1);

            if (choice == 1)
                result = a + b;
            else
                result = a - b;

            printf("Result: %d\n", result);
        }
        else if (choice < 0 || choice > 4)
        {
            printf("Invalid choice\n");
        }

    } while (choice != 0);

    printf("Bye!\n");

    return (0);
}

