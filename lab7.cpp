#include <iostream>
// #include <cstdio> 

using namespace std;

int main() {
    int opcion;

    do {
        printf("Seleccione una opcion:\n");
        printf("1. Comprimir archivo\n");
        printf("2. Descomprimir archivo previamente comprimido\n");
        printf("Ingrese su opcion: ");
        
        cin >> opcion;

        if (cin.fail()) {
            printf("Entrada no valida. Por favor, ingrese un numero.\n");
            cin.clear();
            cin.ignore(256, '\n');
            opcion = 0;
        } else if (opcion != 1 && opcion != 2) {
            printf("Opcion no valida. Por favor, ingrese 1 o 2.\n");
        }

    } while (opcion != 1 && opcion != 2);
    
    
    if (opcion == 1) {
        printf("Ha seleccionado la opcion de compresion.\n");
        


    } else {
        printf("Ha seleccionado la opcion de descompresion.\n");
        



        
    }

    return 0;
}