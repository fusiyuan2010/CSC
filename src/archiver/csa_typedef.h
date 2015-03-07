#ifndef _CSA_TYPEDEF_
#define _CSA_TYPEDEF_
#include <vector>
#include <map>
#include <string>
#include <stdint.h>

using std::vector;
using std::map;
using std::string;

// Index in memory
struct FileEntry {
  int64_t edate;         // date of external file, 0=not found
  int64_t esize;         // size of external file
  int64_t eattr;         // external file attributes ('u' or 'w' in low byte)
  char ext[4];
  struct Frag{
      uint32_t bid;
      uint64_t posblock;
      uint64_t size;
      uint64_t posfile;
  };
  vector<Frag> frags;
};

typedef map<string, FileEntry> FileIndex;
typedef FileIndex::iterator IterFileEntry;


// For raw files I/O
struct FileBlock {
    string filename;
    uint64_t off;
    uint64_t size;
    uint64_t posblock;
};


// For archiver I/O
struct ArchiveBlocks {
    struct Block {
        uint64_t off;
        uint64_t size;
    };
    string filename;
    vector<Block> blocks;
};

typedef map<uint64_t, ArchiveBlocks> ABIndex;
typedef ABIndex::iterator IterAB;

struct CompressionTask {
    uint64_t total_size; 
    vector<FileBlock> filelist;

    CompressionTask() 
        : total_size(0) {}

    void push_back(const string& filename, uint64_t off, uint64_t size) {
        FileBlock b;
        b.filename = filename;
        b.off = 0;
        b.size = size;
        filelist.push_back(b);
        total_size += size;
    }

    void clear() {
        filelist.clear();
        total_size = 0;
    }
};

#endif


