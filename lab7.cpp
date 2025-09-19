#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <pthread.h>
#include <semaphore.h>
#include <zlib.h>

using namespace std;

// Sirve para que el descompresor sepa cuanto leer y escribir (evitar segmentation fault)
struct BlockHeader {
    uint64_t compressed_size;
    uint64_t original_size;
};

// Estructura que se comparte en los hilos
struct CompressShared {
    const uint8_t* data;
    uint64_t file_size;
    uint64_t block_size;
    size_t num_blocks;
    atomic<size_t>* next_block;
    vector<vector<uint8_t>>* out_blocks;
    vector<BlockHeader>* headers;
};


struct Meta { uint64_t offset; uint64_t compressed_size; uint64_t original_size; }; // Necesario para los metadatos en la descompersión

// Se comparte entre los hilos y contiene
struct DecompressShared {
    const uint8_t* data;
    vector<Meta>* metas;
    atomic<size_t>* next_index;
    sem_t* sems;
    FILE* out_file;
    size_t num_blocks;
};

void* compressWorker(void* arg) {
    auto* S = static_cast<CompressShared*>(arg);
    while (true) {
        size_t idx = S->next_block->fetch_add(1);
        if (idx >= S->num_blocks) break;

        uint64_t start = uint64_t(idx) * S->block_size;
        uint64_t end = min(S->file_size, start + S->block_size);
        uint64_t orig_size = end - start;

        if (orig_size == 0) {
            (*S->headers)[idx] = {0, 0};
            continue;
        }

        if (orig_size > numeric_limits<uLong>::max()) {
            cerr << "Bloque demasiado grande para zlib (idx=" << idx << ")\n";
            (*S->headers)[idx] = {0, orig_size};
            continue;
        }

        uLongf dest_len = compressBound(static_cast<uLong>(orig_size));
        vector<uint8_t> dest(dest_len);
        const Bytef* src = reinterpret_cast<const Bytef*>(S->data + start);

        int r = compress2(dest.data(), &dest_len, src, static_cast<uLong>(orig_size), Z_DEFAULT_COMPRESSION);
        if (r != Z_OK) {
            cerr << "Error zlib compress idx=" << idx << " code=" << r << "\n";
            (*S->headers)[idx] = {0, orig_size};
            (*S->out_blocks)[idx].clear();
            continue;
        }
        dest.resize(dest_len);
        (*S->out_blocks)[idx] = std::move(dest);
        (*S->headers)[idx] = {static_cast<uint64_t>(dest_len), orig_size};
    }
    return nullptr;
}

void* decompressWorker(void* arg) {
    auto* S = static_cast<DecompressShared*>(arg);
    while (true) {
        size_t idx = S->next_index->fetch_add(1);
        if (idx >= S->num_blocks) break;

        Meta m = (*S->metas)[idx];

        if (m.original_size == 0) {
            // nothing to write (or error-case)
            sem_wait(&S->sems[idx]);
            sem_post(&S->sems[idx+1]);
            continue;
        }

        if (m.original_size > numeric_limits<uLongf>::max() || m.compressed_size > numeric_limits<uLong>::max()) {
            cerr << "Bloque demasiado grande para zlib en descompresion idx=" << idx << "\n";
            sem_wait(&S->sems[idx]);
            sem_post(&S->sems[idx+1]);
            continue;
        }

        vector<uint8_t> dest(m.original_size);
        uLongf dest_len = static_cast<uLongf>(m.original_size);
        const Bytef* src = reinterpret_cast<const Bytef*>(S->data + m.offset);
        int r = uncompress(dest.data(), &dest_len, src, static_cast<uLong>(m.compressed_size));
        if (r != Z_OK) {
            cerr << "Error zlib uncompress idx=" << idx << " code=" << r << "\n";
            // still preserve ordering: consume semaphore and pass it on
            sem_wait(&S->sems[idx]);
            sem_post(&S->sems[idx+1]);
            continue;
        }

        // Esperar turno para escribir en orden
        sem_wait(&S->sems[idx]);
        size_t written = fwrite(dest.data(), 1, dest_len, S->out_file);
        if (written != dest_len) {
            cerr << "Error escritura bloque idx=" << idx << " bytes escritos=" << written << " esperados=" << dest_len << "\n";
        }
        fflush(S->out_file);
        sem_post(&S->sems[idx+1]);
    }
    return nullptr;
}

void compressFile(const string& in_name, const string& out_name) {
    ifstream ifs(in_name, ios::binary | ios::ate);
    if (!ifs) { cerr << "No se pudo abrir " << in_name << "\n"; return; }
    uint64_t file_size = static_cast<uint64_t>(ifs.tellg());
    ifs.seekg(0, ios::beg);
    vector<uint8_t> data(file_size ? file_size : 0);
    if (file_size) ifs.read(reinterpret_cast<char*>(data.data()), file_size);
    ifs.close();

    cout << "Tamaño original: " << file_size << " bytes\n";
    int threads_input;
    cout << "Ingrese la cantidad de hilos a utilizar: ";
    if (!(cin >> threads_input) || threads_input <= 0) threads_input = 1;
    size_t num_threads = static_cast<size_t>(threads_input);

    if (file_size == 0) {
        cerr << "Archivo vacío. Nada que comprimir.\n";
        return;
    }

    uint64_t block_size = file_size / num_threads;
    if (block_size == 0) block_size = 1;
    size_t num_blocks = static_cast<size_t>((file_size + block_size - 1) / block_size);

    vector<vector<uint8_t>> out_blocks(num_blocks);
    vector<BlockHeader> headers(num_blocks);
    atomic<size_t> next_block(0);

    CompressShared shared{ data.empty() ? nullptr : data.data(),
                          file_size, block_size, num_blocks,
                          &next_block, &out_blocks, &headers };

    size_t workers = min(num_threads, num_blocks);
    vector<pthread_t> worker_threads(workers);
    auto t0 = chrono::high_resolution_clock::now();
    for (size_t i = 0; i < workers; ++i) {
        if (pthread_create(&worker_threads[i], nullptr, compressWorker, &shared) != 0)
            cerr << "Error creando hilo compresion " << i << "\n";
    }
    for (size_t i = 0; i < workers; ++i) pthread_join(worker_threads[i], nullptr);

    // Escribir archivo comprimido (cabeceras + bloques)
    ofstream ofs(out_name, ios::binary);
    if (!ofs) { cerr << "No se pudo crear " << out_name << "\n"; return; }
    uint64_t compressed_total = 0;
    for (size_t i = 0; i < num_blocks; ++i) {
        ofs.write(reinterpret_cast<const char*>(&headers[i]), sizeof(BlockHeader));
        if (headers[i].compressed_size && !out_blocks[i].empty()) {
            ofs.write(reinterpret_cast<const char*>(out_blocks[i].data()), out_blocks[i].size());
            compressed_total += out_blocks[i].size();
        }
    }
    ofs.close();
    auto t1 = chrono::high_resolution_clock::now();
    chrono::duration<double> dur = t1 - t0;
    cout << "Compresión finalizada en " << dur.count() << " s\n";
    cout << "Tamaño comprimido total: " << compressed_total << " bytes\n";
}

void decompressFile(const string& in_name, const string& out_name) {
    ifstream ifs(in_name, ios::binary | ios::ate);
    if (!ifs) { cerr << "No se pudo abrir " << in_name << "\n"; return; }
    uint64_t file_size = static_cast<uint64_t>(ifs.tellg());
    ifs.seekg(0, ios::beg);
    vector<uint8_t> data(file_size ? file_size : 0);
    if (file_size) ifs.read(reinterpret_cast<char*>(data.data()), file_size);
    ifs.close();

    cout << "Tamaño comprimido leído: " << file_size << " bytes\n";
    // Parse headers -> metas
    vector<Meta> metas;
    uint64_t pos = 0;
    while (pos + sizeof(BlockHeader) <= file_size) {
        BlockHeader h;
        memcpy(&h, data.data() + pos, sizeof(BlockHeader));
        pos += sizeof(BlockHeader);
        if (pos + h.compressed_size > file_size) {
            cerr << "Bloque parcial encontrado. Ignorando resto.\n";
            break;
        }
        metas.push_back(Meta{pos, h.compressed_size, h.original_size});
        pos += h.compressed_size;
    }
    size_t num_blocks = metas.size();
    if (num_blocks == 0) { cerr << "No se encontraron bloques.\n"; return; }

    int threads_input;
    cout << "Ingrese la cantidad de hilos a utilizar: ";
    if (!(cin >> threads_input) || threads_input <= 0) threads_input = 1;
    size_t num_threads = static_cast<size_t>(threads_input);

    // Inicializar semáforos para orden de escritura
    vector<sem_t> sems(num_blocks + 1);
    for (size_t i = 0; i <= num_blocks; ++i) {
        sem_init(&sems[i], 0, 0);
    }
    sem_post(&sems[0]); // permitir que el primer bloque se escriba

    FILE* out = fopen(out_name.c_str(), "wb");
    if (!out) { cerr << "No se pudo crear " << out_name << "\n"; 
        for (size_t i = 0; i <= num_blocks; ++i) sem_destroy(&sems[i]);
        return;
    }

    atomic<size_t> next_index(0);
    DecompressShared shared{ data.empty() ? nullptr : data.data(), &metas, &next_index, sems.data(), out, num_blocks };

    size_t workers = min(num_threads, num_blocks);
    vector<pthread_t> threads(workers);
    auto t0 = chrono::high_resolution_clock::now();
    for (size_t i = 0; i < workers; ++i) {
        if (pthread_create(&threads[i], nullptr, decompressWorker, &shared) != 0)
            cerr << "Error creando hilo descompresion " << i << "\n";
    }
    for (size_t i = 0; i < workers; ++i) pthread_join(threads[i], nullptr);

    fclose(out);
    for (size_t i = 0; i <= num_blocks; ++i) sem_destroy(&sems[i]);

    auto t1 = chrono::high_resolution_clock::now();
    chrono::duration<double> dur = t1 - t0;
    cout << "Descompresión finalizada en " << dur.count() << " s\n";
}

int main() {
    int opcion = 0;
    do {
        cout << "Seleccione una opcion:\n1. Comprimir archivo\n2. Descomprimir archivo previamente comprimido\nIngrese su opcion: ";
        if (!(cin >> opcion)) { cin.clear(); cin.ignore(256, '\n'); opcion = 0; }
    } while (opcion != 1 && opcion != 2);

    if (opcion == 1) {
        cout << "Compresión seleccionada.\n";
        string in = "paralelismo_teoria.txt";
        string out = "paralelismo_comprimido.bin";
        compressFile(in, out);
    } else {
        cout << "Descompresión seleccionada.\n";
        string in = "paralelismo_comprimido.bin";
        string out = "paralelismo_descomprimido.txt";
        decompressFile(in, out);
    }
    return 0;
}
