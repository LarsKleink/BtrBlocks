// cmake command: cmake --build . --target my_compression
// test commit

// ------------------------------------------------------------------------------
// Simple Example: Compress Columns with btrblocks
// ------------------------------------------------------------------------------
#include "btrblocks.hpp"
#include "common/Log.hpp"
#include "storage/MMapVector.hpp"
#include "storage/Relation.hpp"
#include "CLI11.hpp"
#include <fstream>
#include <iostream>
// ------------------------------------------------------------------------------ 
template<typename T>
btrblocks::Vector<T> generateData(std::string filename) {
/*btrblocks::Vector<T> generateData(size_t size, size_t unique, size_t runlength, int seed = 42) {
    btrblocks::Vector<T> data(size);
    std::mt19937 gen(seed);
    for (auto i = 0u; i < size - runlength; ++i) {
        auto number = static_cast<T>(gen() % unique);
        for (auto j = 0u; j != runlength; ++j,++i) {
            data[i] = number;
        }
    }
    return data
*/

    std::ifstream filein(filename, std::ios::binary);

    // get its size:
    filein.seekg(0, std::ios::end); // setze "position" auf Ende
    uint64_t datasize = filein.tellg(); // gibt Differenz von Anfang zu "position" in Bytes aus
    filein.seekg(0, std::ios::beg); // setze "position" wieder auf den Anfang

    btrblocks::Vector<double> data(datasize);

    const char * c_filename = filename.c_str();

    data.readBinary(c_filename);

    return data;
}
// ------------------------------------------------------------------------------
template <typename T>
bool validateData(size_t size, T* input, T* output) {
    for (auto i = 0u; i != size; ++i) {
        if (input[i] != output[i]) {
            std::cout << "value @" << i << " does not match; in " << input[i] << " vs out"
                      << output[i] << std::endl;
            return false;
        }
    }
    return true;
}
// ------------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    CLI::App app{"App description"};

    std::string filename = "default";
    app.add_option("file", filename, "dataset file");

    CLI11_PARSE(app, argc, argv);

    std::ifstream filein(filename, std::ios::binary);

    // get its size:
    filein.seekg(0, std::ios::end); // setze "position" auf Ende
    uint64_t datasize = filein.tellg(); // gibt Differenz von Anfang zu "position" in Bytes aus
    filein.seekg(0, std::ios::beg); // setze "position" wieder auf den Anfang
/*
    btrblocks::Vector<double> data(datasize);

    const char * c_filename = filename.c_str();

    data.readBinary(c_filename);
*/

    using namespace btrblocks;
    // required before interacting with btrblocks
    // the passed function is optional and can be used to modify the
    // configuration before BtrBlocks initializes itself
    BtrBlocksConfig::configure([&](BtrBlocksConfig &config){
        /*if (argc > 1) {
            auto max_depth  = std::atoi(argv[1]);
            std::cout << "setting max cascade depth to " << max_depth << std::endl;
            config.integers.max_cascade_depth = max_depth;
            config.doubles.max_cascade_depth = max_depth;
            config.strings.max_cascade_depth = max_depth;
        } */
        config.doubles.schemes.enable(DoubleSchemeType::DOUBLE_BP);
    });

    // If compiled with BTR_FLAG_LOGGING (cmake -DWITH_LOGGING=ON ..),
    // this will set the log level to info; otherwise, it is a no-op.
    // For even more info, set it to 'debug'.
    Log::set_level(Log::level::info);

    // -------------------------------------------------------------------------------------
    // compression
    // -------------------------------------------------------------------------------------

    // initialize some data with runs of numbers
    size_t size = datasize;
    size_t runlength = getenv("runlength") ? atoi(getenv("runlength")) : 40;
    size_t unique = getenv("unique") ? atoi(getenv("unique")) : ((1 << 12) - 1);

    Relation to_compress;
    //to_compress.addColumn({"ints", generateData<int32_t>(size, unique, runlength, 42)});
    //to_compress.addColumn({"dbls", generateData<double>(size, unique, runlength, 69)});

    to_compress.addColumn({"dbls", generateData<double>(filename)});

    // usually we would split up the data into multiple chunks here using Relation::getRanges
    // and then compress each one individually (in parallel).
    // Here, we just compress the whole column at once.
    Range range(0, to_compress.tuple_count);
    Chunk input = to_compress.getChunk({range}, 0);
    Datablock compressor(to_compress);

    // allocate some memory for the output; if this is passed as null,
    // the compressor will allocate the memory itself, estimating required space
    // passing too little memory here can lead to a crash/UB; memory bounds are not checked.
    std::unique_ptr<uint8_t[]> output(new uint8_t[input.tuple_count * sizeof(double) * 2]);

    // compress the data; return value contains some statistics about the
    // overall compression, used schemes and individual columns
    auto start = std::chrono::high_resolution_clock::now();
    auto stats = compressor.compress(input, output);
    auto finish = std::chrono::high_resolution_clock::now();
    auto comp_time = std::chrono::duration_cast<std::chrono::microseconds>(finish-start);

    // compile with BTR_FLAG_LOGGING (cmake -DWITH_LOGGING=ON ..) to
    // get more insights into the compression process
    // the
    std::cout << "Stats:" <<  std::endl
        << "- input size " << input.size_bytes() << std::endl
        << "- output size " << stats.total_data_size << std::endl
        << "- compression ratio " << stats.compression_ratio << std::endl
        ;


    // -------------------------------------------------------------------------------------
    // decompression
    // -------------------------------------------------------------------------------------
    start = std::chrono::high_resolution_clock::now();
    Chunk decompressed = compressor.decompress(output);
    finish = std::chrono::high_resolution_clock::now();
    auto decomp_time = std::chrono::duration_cast<std::chrono::microseconds>(finish-start);

    // check if the decompressed data is the same as the original data
    bool check;
    for (auto col = 0u; col != to_compress.columns.size()-1; ++col) {
        auto& orig = input.columns[col];
        auto& decomp = decompressed.columns[col];
        switch (to_compress.columns[col].type) {
            case ColumnType::INTEGER:
              check = validateData(size, reinterpret_cast<int32_t*>(orig.get()),
                                   reinterpret_cast<int32_t*>(decomp.get()));
              break;
            case ColumnType::DOUBLE:
              check = validateData(size, reinterpret_cast<double*>(orig.get()),
                                   reinterpret_cast<double*>(decomp.get()));
              break;
            default:
              UNREACHABLE();
        }
    }
    std::cout << (check ? "decompressed data matches original data" : "decompressed data does not match original data") << std::endl;
    
    std::ofstream statsfile;
    statsfile.open("/home/lars/prj/Bachelorarbeit/results/btrblocks.csv");
    //statsfile << input.size_bytes() << "\n";
    //statsfile << stats.total_data_size<< "\n";
    statsfile << stats.compression_ratio << "," << std::to_string(comp_time.count()) << "," << std::to_string(decomp_time.count());
    //statsfile << std::to_string(comp_time.count()) << "\t";
    //statsfile << std::to_string(decomp_time.count()) << "\t";

    statsfile.close();
    
    return !check;
}
// ------------------------------------------------------------------------------
