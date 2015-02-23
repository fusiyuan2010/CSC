
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

#include <csa_thread.h>
#include <csa_file.h>

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


char *Put8(uint64_t i, char *buf)
{
    for(int j = 0; j < 8; j++) {
        *buf++ = (i & 0xFF);
        i >>= 8;
    }
    return buf;
}

uint64_t Get8(char *buf)
{
}

char *Put4(uint32_t i, char *buf)
{
    for(int j = 0; j < 4; j++) {
        *buf++ = (i & 0xFF);
        i >>= 8;
    }
    return buf;
}

uint32_t Get4(char *buf)
{
}

char *Put2(uint16_t i, char *buf)
{
    *buf++ = (i & 0xFF);
    *buf++ = (i >> 8);
    return buf;
}

uint16_t Get2(char *buf)
{
}

struct FileEntry {
  int64_t edate;         // date of external file, 0=not found
  int64_t esize;         // size of external file
  int64_t eattr;         // external file attributes ('u' or 'w' in low byte)
  struct PosInBlock {
      uint32_t bid;
      uint64_t posblock;
      uint64_t size;
      uint64_t posfile;
  };
  vector<PosInBlock> frags;
};

typedef map<string, FileEntry> FileIndex;
typedef FileIndex::iterator IterFileEntry;

uint64_t SerializedSize(IterFileEntry it)
{
    return 4 + it->first.size()
        // file name len
        + 3 * 8 + 1 
        // data size eattr, frags num
        + it->second.frags.size() * (4 + 24);
        // frags info
}

char *Serialize(IterFileEntry it, char *buf)
{
    buf = Put4(it->first.size(), buf);
    memcpy(buf, it->first.c_str(), it->first.size());
    buf += it->first.size();
    buf = Put8(it->second.edate, buf);
    buf = Put8(it->second.esize, buf);
    buf = Put8(it->second.eattr, buf);
    *buf++ = it->second.frags.size();
    for(size_t i = 0; i < it->second.frags.size(); i++) {
        buf = Put4(it->second.frags[i].bid, buf);
        buf = Put8(it->second.frags[i].posblock, buf);
        buf = Put8(it->second.frags[i].size, buf);
        buf = Put8(it->second.frags[i].posfile, buf);
    }
    return buf;
}

class CSArc {
    bool isselected(const char* filename);// by files, -only, -not
    void scandir(string filename, bool recurse=true);  // scan dirs to dt
    void addfile(string filename, int64_t edate, int64_t esize,
               int64_t eattr);          // add external file to dt


    FileIndex index_;
    string arcname_;           // archive name
    vector<string> filenames_;     // filename args
    bool recurse_;

    int level_;
    uint32_t dict_size_;

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

int CSArc::Add()
{
    for (int i = 0; i < filenames_.size(); ++i) {
        printf("Filenames: %s\n", filenames_[i].c_str());
        scandir(filenames_[i].c_str(), recurse_);
    }

    for(IterFileEntry it = index_.begin(); it != index_.end(); it++) {
        printf("%s: %lld\n", it->first.c_str(), it->second.esize);
    }

    printf("Arc: %s\n", arcname_.c_str());
    return 0;
}

int CSArc::Extract()
{
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



