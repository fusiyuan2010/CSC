
#define _FILE_OFFSET_BITS 64  // In Linux make sizeof(off_t) == 8
#define UNICODE  // For Windows
#include <csa_common.h>
#include <csa_typedef.h>
#include <csa_thread.h>
#include <csa_file.h>
#include <csa_worker.h>
#include <csa_io.h>
#include <csa_indexpack.h>

// Return true if strings a == b or a+"/" is a prefix of b
// or a ends in "/" and is a prefix of b.
// Match ? in a to any char in b.
// Match * in a to any string in b.
// In Windows, not case sensitive.
bool ispath(const char* a, const char* b) {
  for (; *a; ++a, ++b) {
    const int ca=tolowerW(*a);
    const int cb=tolowerW(*b);
    if (ca=='*') {
      while (true) {
        if (ispath(a+1, b)) return true;
        if (!*b) return false;
        ++b;
      }
    }
    else if (ca=='?') {
      if (*b==0) return false;
    }
    else if (ca==cb && ca=='/' && a[1]==0)
      return true;
    else if (ca!=cb)
      return false;
  }
  return *b==0 || *b=='/';
}

class CSArc {
    bool isselected(const char* filename);// by files, -only, -not
    void scandir(string filename, bool recurse=true);  // scan dirs to dt
    void addfile(string filename, int64_t edate, int64_t esize,
               int64_t eattr);          // add external file to dt


    FileIndex index_;
    ABIndex abindex_;
    string arcname_;           // archive name
    vector<string> filenames_;     // filename args
    bool recurse_;
    int mt_count_;

    int level_;
    uint32_t dict_size_;
    void compress_index();
    void decompress_index();
    void compress_mt(vector<MainTask> &tasks);
    void decompress_mt(vector<MainTask> &tasks);

public:
    int Add();                // add, return 1 if error else 0
    int Extract();            // extract, return 1 if error else 0
    int List();               // list, return 1 if compare = finds a mismatch
    int Test();               // test, return 1 if error else 0
    void Usage();             // help
    int ParseArg(int argc, char *argv[]);
    //====
};

bool compareFuncByPosblock(FileBlock a, FileBlock b) {
    return a.posblock < b.posblock;
}


bool compareFuncByExt(IterFileEntry a, IterFileEntry b) {
    int ret = memcmp(a->second.ext, b->second.ext, 4);
    if (ret != 0)
        return ret < 0;
    else {
        // do not sort very small files
        if (a->second.esize > 64 * 1024 && b->second.esize > 64 * 1024)
            return a->second.esize < b->second.esize;
        else {
            return a->first < b->first;
        }
    }
}

bool compareFuncByTaskSize(MainTask a, MainTask b) {
    return a.total_size > b.total_size;
}

void CSArc::Usage()
{
    fprintf(stderr, "Usage of CSArc\n");
}

/*
int ParseOpt(CSCProps *p, char *argv)
{        
    if (strncmp(argv, "-fdelta0", 8) == 0)
        p->DLTFilter = 0;
    else if (strncmp(argv, "-fexe0", 6) == 0)
        p->EXEFilter = 0;
    else if (strncmp(argv, "-ftxt0", 6) == 0)
        p->TXTFilter = 0;
    return 0;
}

*/
int CSArc::ParseArg(int argc, char *argv[])
{
    int i = 0;

    dict_size_ = 32000000;
    level_ = 2;
    mt_count_ = 1;

    for(; i < argc; i++)
        if (argv[i][0] == '-') {
            if (strncmp(argv[i], "-m", 2) == 0) {
                if (argv[i][2])
                    level_ = argv[i][2] - '0';
                else
                    return -1;
            } else if (strncmp(argv[i], "-d", 2) == 0) {
                int slen = strlen(argv[i]);
                dict_size_ = atoi(argv[i] + 2);
                if ((argv[i][slen - 1] | 0x20) == 'k') 
                    dict_size_ *= 1024;
                else if ((argv[i][slen - 1] | 0x20) == 'm')
                    dict_size_ *= 1024 * 1024;
                if (dict_size_ < 32 * 1024 || dict_size_ >= 1024 * 1024 * 1024)
                    return -1;
            } else if (strncmp(argv[i], "-r", 2) == 0) {
                recurse_ = true;
            } else if (strncmp(argv[i], "-t", 2) == 0) {
                if (argv[i][2])
                    mt_count_ = argv[i][2] - '0';
                else
                    return -1;
            } 
        } else 
            break;

    if (i == argc)
        return -1;

    mt_count_ = mt_count_ < 1? 1 : mt_count_;
    mt_count_ = mt_count_ > 8? 8 : mt_count_;

    arcname_ = argv[i];
    i++;
    for(; i < argc; i++)
        filenames_.push_back(argv[i]);

    /*
    // init the default settings
    CSCEncProps_Init(&p, dict_size, level);
    // Then make extra settings
    for(int i = 0; i < param_end; i++)
        if (ParseOpt(&p, argv[i]) < 0)
            return -1;
   */
    return 0;
}

int show_progress(void *p, UInt64 insize, UInt64 outsize)
{
    (void)p;
    printf("\r%llu -> %llu\t\t\t\t", insize, outsize);
    fflush(stdout);
    return 0;
}

void CSArc::compress_index()
{
    uint64_t index_size = 0;
    char *index_buf = PackIndex(index_, abindex_, index_size);

    FileWriter file_writer;
    MemReader mem_reader;
    Mutex arc_lock;

    mem_reader.ptr = index_buf;
    mem_reader.size = index_size;
    mem_reader.pos = 0;
    mem_reader.is.Read = mem_read_proc;

    // begin to compress index, and write to end of file
    init_mutex(arc_lock);
    ArchiveBlocks ab;
    uint64_t arc_index_pos = 0;
    {
        InputFile f;
        f.open(arcname_.c_str());
        f.seek(0, SEEK_END);
        arc_index_pos = f.tell();
        f.close();

        ab.filename = arcname_;
        ab.blocks.clear();
    }

    file_writer.obj = new AsyncArchiveWriter(ab, 1 * 1048576, arc_lock);
    file_writer.os.Write = file_write_proc;
    {
        CSCProps p;
        CSCEncProps_Init(&p, 256 * 1024, 2);
        CSCEncHandle h = CSCEnc_Create(&p, (ISeqOutStream*)&file_writer);
        uint8_t buf[CSC_PROP_SIZE];
        CSCEnc_WriteProperties(&p, buf, 0);

        file_writer.obj->Run();
        file_writer.obj->Write(buf, CSC_PROP_SIZE);

        CSCEnc_Encode(h, (ISeqInStream*)&mem_reader, NULL);
        CSCEnc_Encode_Flush(h);
        CSCEnc_Destroy(h);
        file_writer.obj->Finish();
        delete file_writer.obj;
    }
    destroy_mutex(arc_lock);
    delete[] index_buf;

    // Write pos of compressed index and its size on header pos
    {
        OutputFile f;
        f.open(arcname_.c_str());
        f.seek(0, SEEK_END);
        uint32_t idx_compressed = f.tell() - arc_index_pos;
        char fs_buf[16];
        Put8(arc_index_pos, fs_buf);
        Put4(idx_compressed, fs_buf + 8);
        Put4(index_size, fs_buf + 12);
        printf("arc_index_pos, %llu compressed_size %lu\n", arc_index_pos, idx_compressed);
        f.write(fs_buf, 8, 16);
        f.close();
    }
}

void CSArc::decompress_index()
{
    char buf[16];
    uint64_t index_pos;
    uint32_t compressed_size;
    uint32_t raw_size;
    InputFile f;

    f.open(arcname_.c_str());
    f.seek(8, SEEK_SET);
    f.read(buf, 16);
    char *tmp = buf;
    tmp = Get8(index_pos, tmp);
    tmp = Get4(compressed_size, tmp);
    tmp = Get4(raw_size, tmp);
    index_.clear();
    abindex_.clear();

    MemReader reader;
    reader.is.Read = mem_read_proc;
    reader.ptr = new char[compressed_size];
    reader.size = compressed_size;
    reader.pos = CSC_PROP_SIZE;
    f.seek(index_pos, SEEK_SET);
    f.read(reader.ptr, compressed_size);
    f.close();

    MemWriter writer;
    writer.os.Write = mem_write_proc;
    writer.ptr = new char[raw_size];
    writer.size = raw_size;
    writer.pos = 0;

    CSCProps p;
    CSCDec_ReadProperties(&p, (uint8_t*)reader.ptr);
    CSCDecHandle h = CSCDec_Create(&p, (ISeqInStream*)&reader);
    CSCDec_Decode(h, (ISeqOutStream*)&writer, NULL);
    CSCDec_Destroy(h);

    UnpackIndex(index_, abindex_, writer.ptr, raw_size);
    delete[] reader.ptr;
    delete[] writer.ptr;

    /*
    for(IterFileEntry it = index_.begin(); it != index_.end(); it++)
        printf("%s %llu %llu\n", it->first.c_str(), it->second.esize, it->second.edate);
    printf("==============\n");
    */
}

void CSArc::compress_mt(vector<MainTask> &tasks)
{
    CompressionWorker *workers[8];
    uint32_t workertasks[8];

    Mutex arc_lock;
    init_mutex(arc_lock);
    Semaphore sem_workers;
    sem_workers.init(mt_count_);

    for(int i = 0; i < mt_count_; i++) {
        workers[i] = new CompressionWorker(sem_workers, arc_lock, level_, dict_size_);
        workers[i]->Run();
        workertasks[i] = tasks.size();
    }

    abindex_.clear();
    std::sort(tasks.begin(), tasks.end(), compareFuncByTaskSize);
    for(uint32_t i = 0; i < tasks.size() + mt_count_; i++) {
        sem_workers.wait();
        for(int j = 0; j < mt_count_; j++) {
            // check which one is finished
            if (workers[j]->TaskDone()) {
                // update index based on info generated while compression
                uint32_t taskid = workertasks[j];
                if (taskid < tasks.size()) {
                    vector<FileBlock>& filelist = tasks[taskid].filelist;
                    for(size_t i = 0; i < filelist.size(); i++) {
                        FileBlock &b = filelist[i];
                        IterFileEntry it = index_.find(b.filename);
                        assert(it != index_.end());
                        FileEntry::Frag pib;
                        pib.bid = taskid;
                        pib.posblock = b.posblock;
                        pib.size = b.size;
                        pib.posfile = 0;
                        if (it->second.frags.size() > 0)
                            printf("asdf");
                        it->second.frags.push_back(pib);
                    }
                }
                // mark it as a invalid value
                workertasks[j] = tasks.size();

                if (i < tasks.size()) {
                    // keep adding remind tasks
                    // task id is always equal to archive id 
                    abindex_.insert(make_pair(i, ArchiveBlocks()));
                    abindex_[i].filename = arcname_;
                    workers[j]->PutTask(tasks[i], abindex_[i]);;
                    workertasks[j] = i;
                }
                break;
            }
        }
    }

    destroy_mutex(arc_lock);

    for(int i = 0; i < mt_count_; i++) {
        workers[i]->Finish();
        delete workers[i];
    }
}

void CSArc::decompress_mt(vector<MainTask> &tasks)
{
    DecompressionWorker *workers[8];
    uint32_t workertasks[8];

    Semaphore sem_workers;
    sem_workers.init(mt_count_);

    for(int i = 0; i < mt_count_; i++) {
        workers[i] = new DecompressionWorker(sem_workers);
        workers[i]->Run();
        workertasks[i] = tasks.size();
    }

    std::sort(tasks.begin(), tasks.end(), compareFuncByTaskSize);
    for(uint32_t i = 0; i < tasks.size(); i++) {
        sem_workers.wait();
        for(int j = 0; j < mt_count_; j++) {
            if (workers[j]->TaskDone()) {
                std::sort(tasks[i].filelist.begin(), tasks[i].filelist.end(), compareFuncByPosblock);
                abindex_[tasks[i].ab_id].filename = arcname_;
                workers[j]->PutTask(tasks[i], abindex_[tasks[i].ab_id]);;
                workertasks[j] = i;
                break;
            }
        }
    }

    for(int i = 0; i < mt_count_; i++) {
        sem_workers.wait();
    }

    for(int i = 0; i < mt_count_; i++) {
        workers[i]->Finish();
        delete workers[i];
    }
}

int CSArc::Add()
{
    for (size_t i = 0; i < filenames_.size(); ++i) {
        printf("Filenames: %s\n", filenames_[i].c_str());
        scandir(filenames_[i].c_str(), recurse_);
    }

    vector<IterFileEntry> itlist;
    for(IterFileEntry it = index_.begin(); it != index_.end(); it++) {
        itlist.push_back(it);
        //printf("%s: %lld\n", it->first.c_str(), it->second.esize);
        size_t dot = it->first.find_last_of('.');
        if (dot == string::npos) {
            memset(it->second.ext, 0, 4);
        } else {
            for(size_t i = 0; i < 4 && i + dot + 1 < it->first.size(); i++)
                it->second.ext[i] = tolower(it->first[i + dot + 1]);
        }
    }

    std::sort(itlist.begin(), itlist.end(), compareFuncByExt);

    vector<MainTask> tasks;
    MainTask curtask;

    for(size_t i = 0; i < itlist.size(); i++) {
        IterFileEntry it = itlist[i];
        if (i && strncmp(it->second.ext, itlist[i-1]->second.ext, 4)) {
            if (curtask.total_size)
                tasks.push_back(curtask);
            curtask.clear();
        }
        curtask.push_back(it->first, 0, it->second.esize, 0);
    }
    if (curtask.total_size)
        tasks.push_back(curtask);

    {
        OutputFile f;
        f.open(arcname_.c_str());
        f.truncate(24);
        f.close();
    }

    compress_mt(tasks);
    compress_index();

    /*
    for(IterFileEntry it = index_.begin(); it != index_.end(); it++)
        printf("%s %llu %llu\n", it->first.c_str(), it->second.esize, it->second.edate);
    printf("==============\n");
    //index_.clear();
    abi.insert(make_pair(0, ab));
    char *index_buf = PackIndex(index_, abi, index_size);

    FileIndex fi2;
    ABIndex abi2;
    UnpackIndex(fi2, abi2, index_buf, index_size);

    for(IterFileEntry it = fi2.begin(); it != fi2.end(); it++)
        printf("%s %llu %llu\n", it->first.c_str(), it->second.esize, it->second.edate);
    printf("==============\n");
 

    uint64_t i2 = 0;
    char *index_buf2 = PackIndex(fi2, abi2, i2);
    printf("pack %llu %llu\n", index_size, i2);
    if (i2 == index_size) {
        for(uint64_t i = 0; i < i2; i++) {
            if (index_buf[i] != index_buf2[i]) {
                printf("Not Match at %llu\n", i);
                break;
            }
        }
    }

    return 0;
    */

    printf("Arc: %s\n", arcname_.c_str());
    return 0;
}

int CSArc::Extract()
{
    string to_dir = "/disk2/tmp/";
    decompress_index();
    vector<MainTask> tasks;
    map<uint64_t, size_t> idmap;

    for(IterFileEntry it = index_.begin(); it != index_.end(); it++) {
        //printf("%s -> blocks: %d\n", it->first.c_str(), it->second.frags.size());
        if (filenames_.size() && !isselected(it->first.c_str()))
            continue;
        string new_filename = to_dir + it->first;
        for(size_t fi = 0; fi < it->second.frags.size(); fi++) {
            MainTask *task = NULL;
            if (idmap.count(it->second.frags[fi].bid) == 0) {
                idmap[it->second.frags[fi].bid] = tasks.size();
                tasks.push_back(MainTask());
                task = &tasks[idmap[it->second.frags[fi].bid]];
                task->ab_id = it->second.frags[fi].bid;
            } else 
                task = &tasks[idmap[it->second.frags[fi].bid]];
            FileEntry::Frag& ff = it->second.frags[fi];
            if (ff.size)
                task->push_back(new_filename, ff.posfile, ff.size, ff.posblock, it);
        }
        makepath(new_filename, it->second.edate, it->second.eattr);
        if (*new_filename.rbegin() != '/') {
            OutputFile f;
            f.open(new_filename.c_str());
            f.truncate();
            f.close(it->second.edate, it->second.eattr);
        }
    }
    decompress_mt(tasks);
    return 0;
}

int CSArc::List()
{
    decompress_index();
    for(IterFileEntry it = index_.begin(); it != index_.end(); it++) {
        if (filenames_.size() && !isselected(it->first.c_str()))
            continue;
        printf("%s %llu\n", it->first.c_str(), it->second.esize);
    }
    return 0;
}

int CSArc::Test()
{
    return 0;
}

void CSArc::scandir(string filename, bool recurse) {

    /*
  // Don't scan diretories excluded by -not
  for (int i=0; i<notfiles.size(); ++i)
    if (ispath(notfiles[i].c_str(), filename.c_str()))
      return;
      */

#ifdef unix

  // Add regular files and directories
  while (filename.size()>1 && filename[filename.size()-1]=='/')
    filename=filename.substr(0, filename.size()-1);  // remove trailing /
  struct stat sb;
  if (!lstat(filename.c_str(), &sb)) {
    if (S_ISREG(sb.st_mode))
      addfile(filename, decimal_time(sb.st_mtime), sb.st_size,
              'u'+(sb.st_mode<<8));

    // Traverse directory
    if (S_ISDIR(sb.st_mode)) {
      addfile(filename=="/" ? "/" : filename+"/", decimal_time(sb.st_mtime),
              0, 'u'+(sb.st_mode<<8));
      if (recurse) {
        DIR* dirp=opendir(filename.c_str());
        if (dirp) {
          for (dirent* dp=readdir(dirp); dp; dp=readdir(dirp)) {
            if (strcmp(".", dp->d_name) && strcmp("..", dp->d_name)) {
              string s=filename;
              if (s!="/") s+="/";
              s+=dp->d_name;
              scandir(s, recurse);
            }
          }
          closedir(dirp);
        }
        else
          perror(filename.c_str());
      }
    }
  }
  else if (recurse || errno!=ENOENT)
    perror(filename.c_str());

#else  // Windows: expand wildcards in filename

  // Expand wildcards
  WIN32_FIND_DATA ffd;
  string t=filename;
  if (t.size()>0 && t[t.size()-1]=='/') {
    if (recurse) t+="*";
    else filename=t=t.substr(0, t.size()-1);
  }
  HANDLE h=FindFirstFile(utow(t.c_str(), true).c_str(), &ffd);
  if (h==INVALID_HANDLE_VALUE && (recurse ||
      (GetLastError()!=ERROR_FILE_NOT_FOUND &&
       GetLastError()!=ERROR_PATH_NOT_FOUND)))
    winError(t.c_str());
  while (h!=INVALID_HANDLE_VALUE) {

    // For each file, get name, date, size, attributes
    SYSTEMTIME st;
    int64_t edate=0;
    if (FileTimeToSystemTime(&ffd.ftLastWriteTime, &st))
      edate=st.wYear*10000000000LL+st.wMonth*100000000LL+st.wDay*1000000
            +st.wHour*10000+st.wMinute*100+st.wSecond;
    const int64_t esize=ffd.nFileSizeLow+(int64_t(ffd.nFileSizeHigh)<<32);
    const int64_t eattr='w'+(int64_t(ffd.dwFileAttributes)<<8);

    // Ignore links, the names "." and ".." or any unselected file
    t=wtou(ffd.cFileName);
    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT
        || t=="." || t=="..") edate=0;  // don't add
    string fn=path(filename)+t;

    // Save directory names with a trailing / and scan their contents
    // Otherwise, save plain files
    if (edate) {
      if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) fn+="/";
      addfile(fn, edate, esize, eattr);
      if (recurse && (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        fn+="*";
        scandir(fn, recurse);
      }
    }
    if (!FindNextFile(h, &ffd)) {
      if (GetLastError()!=ERROR_NO_MORE_FILES) winError(fn.c_str());
      break;
    }
  }
  FindClose(h);
#endif
}

void CSArc::addfile(string filename, int64_t edate,
                    int64_t esize, int64_t eattr) {
  if (!isselected(filename.c_str())) return;
  FileEntry& fn = index_[filename];
  fn.edate=edate;
  fn.esize=esize;
  fn.eattr= eattr; //noattributes?0:eattr;
  //fn.written=0;
}

bool CSArc::isselected(const char* filename) {
  bool matched=true;
  if (filenames_.size()>0) {
    matched=false;
    for (size_t i=0; i < filenames_.size() && !matched; ++i)
      if (ispath(filenames_[i].c_str(), filename))
        matched=true;
  }
  /*
  if (matched && onlyfiles.size()>0) {
    matched=false;
    for (int i=0; i<size(onlyfiles) && !matched; ++i)
      if (ispath(onlyfiles[i].c_str(), filename))
        matched=true;
  }
  for (int i=0; matched && i<size(notfiles); ++i) {
    if (ispath(notfiles[i].c_str(), filename))
      matched=false;
  }
  */
  return matched;
}


int main(int argc, char *argv[])
{
    CSArc csarc;

    if (argc < 3) {
        csarc.Usage();
        return 1;
    }

    char op = argv[1][0];

    if (csarc.ParseArg(argc - 2, argv + 2) < 0)
        return 1;

    switch(op) {
        case 'a':
            csarc.Add();
            break;
        case 't':
            csarc.Test();
            break;
        case 'l':
            csarc.List();
            break;
        case 'x':
            csarc.Extract();
            break;
        default:
            fprintf(stderr, "Invalid command '%c'\n", op);
            return 1;
            break;
    }

    return 0;
}



