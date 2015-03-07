
#define _FILE_OFFSET_BITS 64  // In Linux make sizeof(off_t) == 8
#define UNICODE  // For Windows
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>
#include <fcntl.h>
#include <assert.h>
#include <csc_enc.h>
#include <csc_dec.h>

using namespace std;

#ifdef unix
#define PTHREAD 1
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
#include <errno.h>

#ifdef unixtest
struct termios {
  int c_lflag;
};
#define ECHO 1
#define ECHONL 2
#define TCSANOW 4
int tcgetattr(int, termios*) {return 0;}
int tcsetattr(int, int, termios*) {return 0;}
#else
#include <termios.h>
#endif

#else  // Assume Windows
#include <windows.h>
#include <wincrypt.h>
#include <io.h>
#endif

#ifdef _MSC_VER  // Microsoft C++
#define fseeko(a,b,c) _fseeki64(a,b,c)
#define ftello(a) _ftelli64(a)
#else
#ifndef unix
#ifndef fseeko
#define fseeko(a,b,c) fseeko64(a,b,c)
#endif
#ifndef ftello
#define ftello(a) ftello64(a)
#endif
#endif
#endif

// Convert seconds since 0000 1/1/1970 to 64 bit decimal YYYYMMDDHHMMSS
// Valid from 1970 to 2099.
int64_t decimal_time(time_t tt) {
  if (tt==-1) tt=0;
  int64_t t=(sizeof(tt)==4) ? unsigned(tt) : tt;
  const int second=t%60;
  const int minute=t/60%60;
  const int hour=t/3600%24;
  t/=86400;  // days since Jan 1 1970
  const int term=t/1461;  // 4 year terms since 1970
  t%=1461;
  t+=(t>=59);  // insert Feb 29 on non leap years
  t+=(t>=425);
  t+=(t>=1157);
  const int year=term*4+t/366+1970;  // actual year
  t%=366;
  t+=(t>=60)*2;  // make Feb. 31 days
  t+=(t>=123);   // insert Apr 31
  t+=(t>=185);   // insert June 31
  t+=(t>=278);   // insert Sept 31
  t+=(t>=340);   // insert Nov 31
  const int month=t/31+1;
  const int day=t%31+1;
  return year*10000000000LL+month*100000000+day*1000000
         +hour*10000+minute*100+second;
}

// Convert decimal date to time_t - inverse of decimal_time()
time_t unix_time(int64_t date) {
  if (date<=0) return -1;
  static const int days[12]={0,31,59,90,120,151,181,212,243,273,304,334};
  const int year=date/10000000000LL%10000;
  const int month=(date/100000000%100-1)%12;
  const int day=date/1000000%100;
  const int hour=date/10000%100;
  const int min=date/100%100;
  const int sec=date%100;
  return (day-1+days[month]+(year%4==0 && month>1)+((year-1970)*1461+1)/4)
    *86400+hour*3600+min*60+sec;
}

inline int tolowerW(int c) {
#ifndef unix
  if (c>='A' && c<='Z') return c-'A'+'a';
#endif
  return c;
}

#include <csa_typedef.h>
#include <csa_thread.h>
#include <csa_file.h>
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
    void compress_mt(vector<CompressionTask> &tasks);
    void decompress_mt();

public:
    int Add();                // add, return 1 if error else 0
    int Extract();            // extract, return 1 if error else 0
    int List();               // list, return 1 if compare = finds a mismatch
    int Test();               // test, return 1 if error else 0
    void Usage();             // help
    int ParseArg(int argc, char *argv[]);
    //====
};

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
    int i = 0, param_end;
    CSCProps p;

    dict_size_ = 32000000;
    level_ = 2;

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
            } 
        } else 
            break;

    if (i == argc)
        return -1;

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
}

int show_progress(void *p, UInt64 insize, UInt64 outsize)
{
    (void)p;
    printf("\r%llu -> %llu\t\t\t\t", insize, outsize);
    fflush(stdout);
    return 0;
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

bool compareFuncByTaskSize(CompressionTask a, CompressionTask b) {
    return a.total_size > b.total_size;
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

#include <csa_worker.h>
void CSArc::compress_mt(vector<CompressionTask> &tasks)
{
    CompressWorker *workers[8];
    uint32_t workertasks[8];
    mt_count_ = 2;

    Mutex arc_lock;
    init_mutex(arc_lock);
    Semaphore sem_workers;
    sem_workers.init(mt_count_);

    for(int i = 0; i < mt_count_; i++) {
        workers[i] = new CompressWorker(sem_workers, arc_lock, level_, dict_size_);
        workers[i]->Run();
        workertasks[i] = tasks.size();
    }

    abindex_.clear();
    std::sort(tasks.begin(), tasks.end(), compareFuncByTaskSize);
    for(uint32_t i = 0; i < tasks.size(); i++) {
        sem_workers.wait();
        for(int j = 0; j < mt_count_; j++) {
            if (workers[j]->TaskDone()) {
                // update index based on info generated while compression
                uint32_t taskid = workertasks[j];
                if (taskid < tasks.size()) {
                    vector<FileBlock>& filelist = tasks[taskid].filelist;
                    for(off_t i = 0; i < filelist.size(); i++) {
                        FileBlock &b = filelist[i];
                        IterFileEntry it = index_.find(b.filename);
                        assert(it != index_.end());
                        FileEntry::Frag pib;
                        pib.bid = taskid;
                        pib.posblock = b.posblock;
                        pib.size = b.size;
                        pib.posfile = 0;
                        it->second.frags.push_back(pib);
                    }
                }
                abindex_.insert(make_pair(i, ArchiveBlocks()));
                abindex_[i].filename = arcname_;
                workers[j]->PutTask(tasks[i], abindex_[i]);;
                workertasks[j] = i;
                break;
            }
        }
    }

    for(int i = 0; i < mt_count_; i++) {
        sem_workers.wait();
    }
    destroy_mutex(arc_lock);

    for(int i = 0; i < mt_count_; i++) {
        workers[i]->Finish();
        delete workers[i];
    }

    /*
    vector<FileBlock> filelist;
    FileReader file_reader;
    file_reader.obj = new AsyncFileReader(filelist, 32 * 1048576);
    file_reader.is.Read = file_read_proc;

    abindex_.insert(make_pair(0, ArchiveBlocks()));
    Mutex arc_lock;
    init_mutex(arc_lock);
    for(IterAB itab = abindex_.begin(); itab != abindex_.end(); itab++) {

    ArchiveBlocks& ab = itab->second;
    {
        ab.filename = arcname_;
        ab.blocks.clear();
    }
    FileWriter file_writer;
    file_writer.obj = new AsyncArchiveWriter(ab, 8 * 1048576, arc_lock);
    file_writer.os.Write = file_write_proc;

    {
        //ICompressProgress prog;
        //prog.Progress = show_progress;
        CSCProps p;
        CSCEncProps_Init(&p, dict_size_, level_);
        CSCEncHandle h = CSCEnc_Create(&p, (ISeqOutStream*)&file_writer);
        uint8_t buf[CSC_PROP_SIZE];
        CSCEnc_WriteProperties(&p, buf, 0);

        file_reader.obj->Run();
        file_writer.obj->Run();
        file_writer.obj->Write(buf, CSC_PROP_SIZE);

        CSCEnc_Encode(h, (ISeqInStream*)&file_reader, NULL);
        CSCEnc_Encode_Flush(h);
        CSCEnc_Destroy(h);
        file_reader.obj->Finish();
        file_writer.obj->Finish();
        delete file_writer.obj;
        delete file_reader.obj;
    }


    }
    destroy_mutex(arc_lock);
    */
}

void CSArc::decompress_mt()
{
}

int CSArc::Add()
{

    for (int i = 0; i < filenames_.size(); ++i) {
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

    vector<CompressionTask> tasks;
    CompressionTask curtask;

    for(off_t i = 0; i < itlist.size(); i++) {
        IterFileEntry it = itlist[i];
        if (i && strncmp(it->second.ext, itlist[i-1]->second.ext, 4)) {
            if (curtask.total_size)
                tasks.push_back(curtask);
            curtask.clear();
        }
        curtask.push_back(it->first, 0, it->second.esize);
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

bool compareFuncByPosblock(FileBlock a, FileBlock b) {
    return a.posblock < b.posblock;
}

int CSArc::Extract()
{
    decompress_index();
    for(IterAB it = abindex_.begin(); it != abindex_.end(); it++) {
        ArchiveBlocks& ab = it->second;
        vector<FileBlock> filelist;
        for(IterFileEntry it2 = index_.begin(); it2 != index_.end(); it2++) {
            // only one block currently
            if (it2->second.frags[0].bid == it->first) {
                FileBlock b;
                b.filename = "/disk2/tmp/" + it2->first;
                b.off = it2->second.frags[0].posfile;
                b.size = it2->second.frags[0].size;
                b.posblock = it2->second.frags[0].posblock;
                if (b.size)
                    filelist.push_back(b);

                makepath(b.filename);
                if (*b.filename.rbegin() != '/') {
                    OutputFile f;
                    f.open(b.filename.c_str());
                    f.truncate();
                    f.close();
                }
            }
        }

        std::sort(filelist.begin(), filelist.end(), compareFuncByPosblock);

        FileReader file_reader;
        file_reader.obj = new AsyncArchiveReader(ab, 8 * 1048576);
        file_reader.is.Read = file_read_proc;

        FileWriter file_writer;
        file_writer.obj = new AsyncFileWriter(filelist, 16 * 1048576);
        file_writer.os.Write = file_write_proc;

        file_reader.obj->Run();
        file_writer.obj->Run();

        CSCProps p;
        uint8_t buf[CSC_PROP_SIZE];
        size_t prop_size = CSC_PROP_SIZE; 
        file_reader.obj->Read(buf, &prop_size);
        CSCDec_ReadProperties(&p, buf);
        CSCDecHandle h = CSCDec_Create(&p, (ISeqInStream*)&file_reader);
        CSCDec_Decode(h, (ISeqOutStream*)&file_writer, NULL);
        CSCDec_Destroy(h);

        file_reader.obj->Finish();
        file_writer.obj->Finish();
        delete file_reader.obj;
        delete file_writer.obj;
    }
    return 0;
}

int CSArc::List()
{
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
    for (int i=0; i < filenames_.size() && !matched; ++i)
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



