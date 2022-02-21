#ifndef SECURE_hpp_
#define SECURE_hpp_

#include <miniz.h>
#include <openssl/evp.h>
#include <vector>

namespace Slic3r {

    std::string base64_encode(std::string const & data);

    std::string base64_decode(std::string const & data);

    class EVP_Cipher
    {
    public:
        EVP_Cipher();

        EVP_Cipher(std::string const& algorithm, std::string const& userkey, bool encode, std::string const& iv = {});

        ~EVP_Cipher();

        bool setup(std::string const& algorithm, std::string const& userkey, bool encode, std::string const& iv = {});

        void set_output(bool (*callback)(void * context, unsigned char const * data, int len), void * context);

        // call before first update
        bool set_aad(std::string const & aad);

        virtual bool update(unsigned char const * data, int len);

        // call before finalize
        bool set_tag(std::string const & tag);

        virtual bool finalize();

        // call after finalize
        std::string  get_tag();

        bool has_setup() const { return !out.empty(); }

        std::string const & get_result() const { return result; }

    protected:
        virtual bool output(unsigned char const * data, int len);

        virtual bool output_internal(unsigned char const * data, int len);

    private:
        EVP_CIPHER_CTX *ctx;
        unsigned char in[EVP_MAX_BLOCK_LENGTH];
        std::vector<unsigned char> out;
        int remain = 0;
        std::string result;
        int flags = 0;
        bool (*output_callback)(void * context, unsigned char const * data, int len) = nullptr;
        void * output_context = nullptr;
    };

    inline std::string cipher_encrypt(std::string const& algorithm, std::string const& userkey, std::string const& data, std::string const& iv = {}) {
        EVP_Cipher encrypt(algorithm, userkey, true, iv);
        encrypt.update((unsigned char*)data.data(), data.size());
        encrypt.finalize();
        return encrypt.get_result();
    }

    inline std::string cipher_decrypt(std::string const& algorithm, std::string const& userkey, std::string const& data, std::string const& iv = {}) {
        EVP_Cipher encrypt(algorithm, userkey, false, iv);
        encrypt.update((unsigned char*)data.data(), data.size());
        encrypt.finalize();
        return encrypt.get_result();
    }

    class DeflateEncrypt : public EVP_Cipher
    {
    public:
        DeflateEncrypt() ;

        ~DeflateEncrypt();

        bool setup_deflate();

        virtual bool update(unsigned char const * data, int len) override;

        virtual bool finalize() override;

    protected:
        friend class ZipEncrypt;
        tdefl_compressor compressor;

    private:
        bool deflate_callback(unsigned char const * data, int len);

        static mz_bool static_deflate_callback(void const * data, int len, void * thiz);
    };

    class DecryptInflate : public EVP_Cipher
    {
    public:
        DecryptInflate();

        ~DecryptInflate();

        bool setup_inflate();

        virtual bool finalize() override;

    protected:
        tinfl_decompressor inflator;
        // callback mode
        size_t size;
        std::vector<unsigned char> buffer_inflate;
        size_t read_buf_ofs = 0;
        size_t out_buf_ofs = 0;
        tinfl_status status = TINFL_STATUS_DONE;

    private:
        virtual bool output_internal(unsigned char const * data, int len) override;
    };

    class ZipEncrypt : public DeflateEncrypt
    {
    public:
        ZipEncrypt() ;

        ~ZipEncrypt();

        bool open(mz_zip_writer_staged_context & context);

        bool close();

    protected:
        mz_zip_writer_staged_context * input_context = nullptr;
        mz_zip_writer_staged_context output_context;

    private:
        virtual bool output(unsigned char const * data, int len) override;
    };

    class ZipDecrypt : public DecryptInflate
    {
    public:
        ZipDecrypt();

        ~ZipDecrypt();

        bool open(mz_zip_reader_extract_iter_state & input_state) ;

        bool open(mz_uint64 size, mz_file_write_func callback, void * opaque);

        bool close();

        bool done() { return status == TINFL_STATUS_DONE; }

        static size_t static_callback(void *pOpaque, mz_uint64 file_ofs, const void *pBuf, size_t n);

    protected:
        mz_zip_reader_extract_iter_state & input_state;
        mz_zip_reader_extract_iter_state output_state;
        // callback mode
        mz_uint64 size = 0;
        mz_file_write_func callback = nullptr;
        void * opaque = nullptr;

    private:
        virtual bool output(unsigned char const * data, int len) override;
    };

    class KeyStore // <keystore/>
    {
    public:
        struct Consumer // <consumer/>
        {
            std::string consumerid;
            std::string keyid;
            std::string keyvalue; // <keyvalue/>

            friend bool operator==(Consumer const& l, Consumer const& r) {
                return l.consumerid == r.consumerid && l.keyid == r.keyid;
            }
        };

        struct CEKParams // <cekparams/>
        {
            std::string encryptionalgorithm;
            std::string compression;
            std::string iv; // <iv/>
            std::string aad; // <aad/>
            std::string tag; // <tag/>
        };

        struct KEKParams // <kekparams/>
        {
            std::string wrappingalgorithm;
            std::string mgfalgorithm;
            std::string digestmethod;
        };

        struct AccessRight // <accessright/>
        {
            int consumerindex;
            KEKParams kekparams;
            std::string cipherdata; // <cipherdata/> // <xenc:CipherValue/>
        };

        struct ResourceData // <resourcedata/>
        {
            std::string path;
            CEKParams cekparams;
        };

        struct ResourceDataGroup // <resourcedatagroup/>
        {
            std::string keyuuid;
            std::string content_key;
            std::vector<AccessRight> accessrights;
            std::vector<ResourceData> resourcedatas;
        };

        static std::vector<Consumer> global_consumers;

        std::string UUID;
        std::vector<Consumer> consumers;
        std::vector<ResourceDataGroup> resourcedatagroups; // only one key group

        static KeyStore* create(std::string const& keyid);

        void save(std::ostream & stream) const;

        bool setup(std::string const& path, EVP_Cipher& cipher, bool encode = false);

        bool finalize(std::string const& path, EVP_Cipher& encrypt);

        bool rename(std::string const& path_from, std::string const& path_to);
    };

    class KeyStoreLoader
    {
    public:
        static KeyStoreLoader * create(KeyStore* key_store);

    public:
        virtual bool handle_start_xml_element(const char* name, const char** attributes) = 0;

        virtual bool handle_xml_characters(char const * s, int len) = 0;

        virtual bool handle_end_xml_element(const char* name) = 0;
    };

} // namespace Slic3r

#endif
