#include <iostream>
#include <cstdio>
#include <fstream>
#include <vector>
#include <pthread.h>
#include <chrono> // Para medir el tiempo
#include <zlib.h> // Para la compresión DEFLATE
using namespace std;


// Estructura para pasar argumentos a los hilos
struct ThreadArgs {
    vector<char>* input_data;
    size_t start_index;
    size_t end_index;
    vector<char>* compressed_data;
};

// Subrutina que cada hilo ejecutará para comprimir un bloque
void* compressBlock(void* args) {
    ThreadArgs* thread_args = static_cast<ThreadArgs*>(args);
    
    // Obtener el bloque de datos a comprimir
    const vector<char>& input_data = *thread_args->input_data;
    size_t block_size = thread_args->end_index - thread_args->start_index;
    const Bytef* source = reinterpret_cast<const Bytef*>(&input_data[thread_args->start_index]);
    
    // Preparar el buffer de salida
    uLongf dest_len = compressBound(block_size);
    vector<char> compressed_buffer(dest_len);
    Bytef* dest = reinterpret_cast<Bytef*>(compressed_buffer.data());
    
    // Comprimir el bloque
    int result = compress(dest, &dest_len, source, block_size);
    
    if (result == Z_OK) {
        // Redimensionar el buffer al tamaño real de los datos comprimidos
        compressed_buffer.resize(dest_len);
        
        // Guardar el resultado comprimido en la estructura de argumentos
        thread_args->compressed_data = new vector<char>(compressed_buffer);
    } else {
        thread_args->compressed_data = nullptr;
        cerr << "Error de compresion en el hilo. Codigo: " << result << endl;
    }

    pthread_exit(NULL);
}



// Función principal de compresión
void compressFile(const string& input_filename, const string& output_filename) {
    // 1. Lectura del archivo de entrada
    ifstream inputFile(input_filename, ios::binary | ios::ate);
    if (!inputFile.is_open()) {
        printf("Error: No se pudo abrir el archivo de entrada.\n");
        return;
    }
    
    streampos file_size = inputFile.tellg();
    inputFile.seekg(0, ios::beg);
    
    vector<char> input_data(file_size);
    inputFile.read(input_data.data(), file_size);
    inputFile.close();
    
    printf("Archivo de entrada leido. Tamano original: %ld bytes.\n", file_size);

    // 2. Solicitud de hilos
    int num_threads;
    printf("Ingrese la cantidad de hilos a utilizar: ");
    cin >> num_threads;
    
    if (num_threads <= 0) {
        printf("Numero de hilos no valido. Se usara 1 hilo por defecto.\n");
        num_threads = 1;
    }

    // 3. Medición del tiempo de ejecución
    auto start_time = chrono::high_resolution_clock::now();

    // Lógica de paralelización
    size_t block_size = file_size / num_threads;
    vector<pthread_t> threads(num_threads);
    vector<ThreadArgs> thread_args(num_threads);
    
    // Crear los hilos y asignar los bloques
    for (int i = 0; i < num_threads; ++i) {
        thread_args[i].input_data = &input_data;
        thread_args[i].start_index = i * block_size;
        
        // El último hilo toma el resto del archivo
        if (i == num_threads - 1) {
            thread_args[i].end_index = file_size;
        } else {
            thread_args[i].end_index = (i + 1) * block_size;
        }
        
        // Crear el hilo
        int result = pthread_create(&threads[i], NULL, compressBlock, &thread_args[i]);
        if (result != 0) {
            cerr << "Error al crear el hilo " << i << endl;
        }
    }
    
    // Esperar a que todos los hilos terminen
    for (int i = 0; i < num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }
    
    // 4. Escribir el archivo comprimido final
    ofstream outputFile(output_filename, ios::binary);
    if (!outputFile.is_open()) {
        printf("Error: No se pudo abrir el archivo de salida.\n");
        return;
    }
    
    size_t compressed_size = 0;
    for (int i = 0; i < num_threads; ++i) {
        if (thread_args[i].compressed_data) {
            outputFile.write(thread_args[i].compressed_data->data(), thread_args[i].compressed_data->size());
            compressed_size += thread_args[i].compressed_data->size();
            delete thread_args[i].compressed_data; // Liberar memoria
        }
    }
    outputFile.close();

    // 5. Medición final del tiempo y resultados
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> duration = end_time - start_time;
    
    printf("Compresion finalizada.\n");
    printf("Tiempo de ejecucion: %.4f segundos.\n", duration.count());
    printf("Tamano original: %ld bytes.\n", file_size);
    printf("Tamano comprimido: %ld bytes.\n", compressed_size);
}


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
        printf("Ha seleccionado la opcion de compresion.\n");
        string input_file = "paralelismo_teoria.txt";
        string output_file = "paralelismo_comprimido.bin";
        compressFile(input_file, output_file);


    } else {
        printf("Ha seleccionado la opcion de descompresion.\n");
        




    }

    return 0;
}