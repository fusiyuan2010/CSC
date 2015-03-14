#include <csa_worker.h>
#include <csa_file.h>
#include <csc_enc.h>
#include <csc_dec.h>


int CompressionWorker::do_work() 
{
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

    int ret = CSCEnc_Encode(h, (ISeqInStream*)&file_reader, NULL);
    CSCEnc_Encode_Flush(h);
    CSCEnc_Destroy(h);
    file_reader.obj->Finish();
    file_writer.obj->Finish();
    delete file_writer.obj;
    delete file_reader.obj;
    return ret;
}


int DecompressionWorker::do_work() 
{
    FileReader file_reader;
    file_reader.obj = new AsyncArchiveReader(*abs_, 8 * 1048576);
    file_reader.is.Read = file_read_proc;

    FileWriter file_writer;
    file_writer.obj = new AsyncFileWriter(task_->filelist, 32 * 1048576);
    file_writer.os.Write = file_write_proc;

    file_reader.obj->Run();
    file_writer.obj->Run();

    CSCProps p;
    uint8_t buf[CSC_PROP_SIZE];
    size_t prop_size = CSC_PROP_SIZE; 
    file_reader.obj->Read(buf, &prop_size);
    CSCDec_ReadProperties(&p, buf);
    CSCDecHandle h = CSCDec_Create(&p, (ISeqInStream*)&file_reader);
    int ret = CSCDec_Decode(h, (ISeqOutStream*)&file_writer, NULL);
    CSCDec_Destroy(h);

    file_reader.obj->Finish();
    file_writer.obj->Finish();
    delete file_reader.obj;
    delete file_writer.obj;
    return ret;
}

