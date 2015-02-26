
#include <deque>

class AsyncReader;
class AsyncWriter;

struct FileBlock {
    string filename;
    uint64_t off;
    uint64_t size;
    uint64_t posblock;
};

class AsyncReader {
    vector<FileBlock> &filelist_;
    ThreadID iothread_;
    Mutex lock_;
    Semaphore sem_full_;
    Semaphore sem_empty_;
    uint32_t size_;
    uint32_t bufsize_;
    volatile bool finished_;

    struct Block {
        char *buf;
        uint32_t size;
        uint32_t prog;
    };

    std::deque<Block> queue_;
    
    static ThreadReturn entrance(void *arg) 
    {
        AsyncReader *ar = (AsyncReader *)arg; 
        return ar->run();
    }

    off_t curfidx_;
    size_t curfprog_;
    uint64_t cumsize_;
    InputFile if_;

    void *run() {
        while(1) {
            lock(lock_);
            if (size_ >= bufsize_) {
                release(lock_);
                sem_full_.wait();
                continue;
            }
            uint32_t rdsize = bufsize_ - size_;
            release(lock_);
            
            // open next file if last was done
            if (!if_.isopen()) {
                if (curfidx_ >= filelist_.size()) {
                    finished_ = true;
                    break;
                }
                if (!if_.open(filelist_[curfidx_].filename.c_str())) {
                    filelist_[curfidx_].size = 0;
                    curfidx_++;
                    continue;
                }
                filelist_[curfidx_].posblock = cumsize_;
                if_.seek(filelist_[curfidx_].off, SEEK_SET);
            }

            // how much to read?
            rdsize = std::min<uint64_t>(rdsize, filelist_[curfidx_].size - curfprog_);
            rdsize = std::min<uint64_t>(rdsize, 2 * 1048576);
            char *buf = new char[rdsize];
            // read and push to queue
            rdsize = if_.read(buf, rdsize);
            Block b;
            b.prog = 0;
            b.size = rdsize;
            b.buf = buf;

            lock(lock_);
            queue_.push_back(b);
            size_ += b.size;
            release(lock_);
            sem_empty_.signal();

            curfprog_ += rdsize;
            cumsize_ += rdsize;
            // end of current file, close it
            if (curfprog_ == filelist_[curfidx_].size) {
                if_.close();
                curfprog_ = 0;
                curfidx_ ++;
            }
        }
        return NULL;
    }

public:
    AsyncReader(vector<FileBlock> &filelist, uint32_t bufsize)
    : filelist_(filelist),
    size_(0),
    bufsize_(bufsize),
    finished_(false),
    curfidx_(0),
    curfprog_(0),
    cumsize_(0)
    {
        init_mutex(lock_);
        sem_full_.init(0);
        sem_empty_.init(0);
        ::run(iothread_, AsyncReader::entrance, this);
    }

    ~AsyncReader() {
        join(iothread_);
        destroy_mutex(lock_);
        sem_full_.destroy();
        sem_empty_.destroy();
    }

    int Read(void *buf, size_t *size) {
        size_t prog = 0;
        for(; prog < *size;) {
            lock(lock_);
            if (size_ == 0) {
                release(lock_);
                if (finished_) {
                    *size = prog;
                    return 0;
                }
                sem_empty_.wait();
                continue;
            }

            Block &b = queue_.front();
            size_t cpy = std::min<uint64_t>(*size - prog, b.size - b.prog);
            memcpy((char *)buf + prog, b.buf + b.prog, cpy);
            prog += cpy;
            b.prog += cpy;
            if (b.prog == b.size) {
                delete[] b.buf;
                queue_.pop_front();
                sem_full_.signal();
                size_ -= b.size;
            }
            release(lock_);
        }
        return 0;
    }

    void Finish() {
    }
};

struct Reader {
    ISeqInStream is;
    AsyncReader *ar;
};

int read_proc(void *p, void *buf, size_t *size)
{
    Reader *r = (Reader *)p;
    return r->ar->Read(buf, size);
}

/*
   Reader reader;
   reader.ar = xx;
   reader.is.Read = read_proc;

*/

class AsyncWriter {
    vector<FileBlock> &filelist_;
    ThreadID iothread_;
    Mutex lock_;
    Semaphore sem_full_;
    Semaphore sem_empty_;
    uint32_t size_;
    uint32_t bufsize_;
    volatile bool finished_;

    static ThreadReturn entrance(void *arg) 
    {
        AsyncWriter *ar = (AsyncWriter *)arg; 
        return ar->run();
    }

    uint64_t curfidx_;
    uint64_t curfprog_;
    uint64_t curbprog_;
    uint64_t cumsize_;

    struct Block {
        char *buf;
        uint32_t size;
        uint32_t prog;
    };

    std::deque<Block> queue_;
    OutputFile of_;

    void *run() {
        while(1) {
            lock(lock_);
            if (finished_) {
                release(lock_);
                break;
            }

            if (size_ == 0) {
                release(lock_);
                sem_empty_.wait();
                continue;
            }

            Block b = queue_.front();
            if (cumsize_ + b.size > filelist_[curfidx_].posblock) {
                if (!of_.isopen()) {
                    if (!of_.open(filelist_[curfidx_].filename.c_str())) {
                        curfidx_++;
                        if (curfidx_ >= filelist_.size()) {
                            finished_ = true;
                            sem_full_.signal();
                        }
                        continue;
                    }
                    of_.truncate(0);
                    curfprog_ = 0;
                    of_.seek(filelist_[curfidx_].off, SEEK_SET);
                }
                
                uint32_t wrsize = std::min(b.size - curbprog_, filelist_[curfidx_].size - curfprog_);
                of_.write(b.buf, wrsize);
                curfprog_ += wrsize;
                curbprog_ += wrsize;
                if (curfprog_ ==  filelist_[curfidx_].size) {
                    of_.close();
                    curfidx_++;
                    if (curfidx_ >= filelist_.size()) {
                        finished_ = true;
                        sem_full_.signal();
                    }
                }
            } else {
                curbprog_ = b.size;
            }


            if (curbprog_ == b.size) {
                delete[] b.buf;
                queue_.pop_front();
                size_ -= b.size;
                release(lock_);
                curbprog_ = 0;
                sem_full_.signal();
            }
        }
        return NULL;
    }
 
public:

    AsyncWriter(vector<FileBlock> &filelist, uint32_t bufsize)
    : filelist_(filelist),
    size_(0),
    bufsize_(bufsize),
    finished_(false),
    curfidx_(0),
    curfprog_(0),
    cumsize_(0)
    {
        init_mutex(lock_);
        sem_full_.init(0);
        sem_empty_.init(0);
        ::run(iothread_, AsyncWriter::entrance, this);
    }

    ~AsyncWriter() {
        destroy_mutex(lock_);
        sem_full_.destroy();
        sem_empty_.destroy();
    }

    size_t Write(const void *buf, size_t size) {
        for(;;) {
            lock(lock_);
            if (finished_ == true) {
                release(lock_);
                return CSC_WRITE_ABORT;
            }

            if (size_ >= bufsize_) {
                release(lock_);
                sem_full_.wait();
                continue;
            }
            Block b;
            b.buf = new char[size];
            b.size = size;
            memcpy(b.buf, buf, size);
            queue_.push_back(b);
            size_ += size;
            release(lock_);
            sem_empty_.signal();
            return size;
        }
    }

    void Finish() {
        lock(lock_);
        finished_ = true;
        release(lock_);
        sem_empty_.signal();
        join(iothread_);
        if (of_.isopen()) {
            filelist_[curfidx_].size = curfprog_;
            of_.close();
        }
    }
};


struct Writer {
    ISeqOutStream os;
    AsyncWriter *aw;
};

size_t write_proc(void *p, const void *buf, size_t size) 
{
    Writer *w = (Writer *)p;
    return w->aw->Write(buf, size);
}


