#ifndef _CSA_WORKER_H_
#define _CSA_WORKER_H_

#include <csa_typedef.h>
#include <csa_thread.h>
#include <csa_io.h>

class CompressWorker {
    ThreadID thread_;
    Semaphore& sem_finish_;
    Mutex& arc_lock_;
    Semaphore got_task_;
    int level_;
    uint32_t dict_size_;
    CompressionTask *task_;
    ArchiveBlocks* abs_;
    bool finished_;

    static ThreadReturn entrance(void *arg) 
    {
        CompressWorker *worker = (CompressWorker *)arg; 
        return worker->run();
    }

    void do_compression() {
        FileReader file_reader;
        file_reader.obj = new AsyncFileReader(task_->filelist, 
                std::min<uint32_t>(32 * 1048576, task_->total_size));
        file_reader.is.Read = file_read_proc;

        FileWriter file_writer;
        file_writer.obj = new AsyncArchiveWriter(*abs_, 8 * 1048576, arc_lock_);
        file_writer.os.Write = file_write_proc;

        //ICompressProgress prog;
        //prog.Progress = show_progress;
        CSCProps p;
        CSCEncProps_Init(&p, std::min<uint32_t>(dict_size_, task_->total_size), level_);
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

    void *run() {
        while(1) {
            got_task_.wait();
            if (finished_)
                break;

            do_compression();

            task_ = NULL;
            sem_finish_.signal();
        }
        return NULL;
    }

public:
    CompressWorker(Semaphore &sem, Mutex& arc_lock, int level, int dict_size) :
        sem_finish_(sem),
        arc_lock_(arc_lock),
        level_(level),
        dict_size_(dict_size),
        task_(NULL),
        finished_(false) {
            got_task_.init(0);
        }

    void Run() {
        ::run(thread_, CompressWorker::entrance, this);
    }

    void PutTask(CompressionTask& task, ArchiveBlocks& abs) {
        task_ = &task;
        abs_ = &abs;
        got_task_.signal();
    }

    bool TaskDone() {
        return task_ == NULL;
    }
    
    void Finish() {
        finished_ = true;
        got_task_.signal();
        ::join(thread_);
    }
};

#endif

