#include "Secure.hpp"

#include <openssl/rand.h>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/spirit/include/qi_int.hpp>

namespace Slic3r {

    std::string base64_encode(std::string const & data) {
        std::string result;
        result.resize(boost::beast::detail::base64::encoded_size(data.size()));
        result.resize(boost::beast::detail::base64::encode(result.data(), data.data(), data.size()));
        return result;
    }

    std::string base64_decode(std::string const & data) {
        std::string result;
        result.resize(boost::beast::detail::base64::decoded_size(data.size()));
        result.resize(boost::beast::detail::base64::decode(result.data(), data.data(), data.size()).first);
        return result;
    }

    constexpr int EVP_FLAGS_ENCRYPT = 1;
    constexpr int EVP_FLAGS_TAG_SET = 2;

    EVP_Cipher::EVP_Cipher()
    {
        OpenSSL_add_all_algorithms();
        ctx = EVP_CIPHER_CTX_new();
        memset(in, 0, sizeof(in));
    }

    EVP_Cipher::EVP_Cipher(std::string const& algorithm, std::string const& userkey, bool encode, std::string const& iv)
        : EVP_Cipher()
    {
        setup(algorithm, userkey, encode, iv);
    }

    EVP_Cipher::~EVP_Cipher()
    {
        EVP_CIPHER_CTX_free(ctx);
    }

    bool EVP_Cipher::setup(std::string const& algorithm, std::string const& userkey, bool encode, std::string const& iv)
    {
        auto cipher = EVP_get_cipherbyname(algorithm.c_str());
        assert(userkey.size() == EVP_CIPHER_key_length(cipher));
        int ivl = EVP_CIPHER_iv_length(cipher);
        assert(iv.empty() || iv.size() == ivl);
        if (userkey.size() != EVP_CIPHER_key_length(cipher) || (!iv.empty() && iv.size() != ivl))
            return false;
        EVP_CipherInit(ctx, cipher, NULL, NULL, encode ? 1 : 0);
        int ret = EVP_CipherInit(ctx, NULL, (unsigned char *) userkey.data(), iv.empty() ? in : (unsigned char*)iv.data(), -1);
        assert(ret == 1);
        out.resize(32 * 1025); // add 32
        return ret == 1;
    }

    void EVP_Cipher::set_output(bool(*callback)(void * context, unsigned char const* data, int len), void * context)
    {
        output_callback = callback;
        output_context = context;
    }

    bool EVP_Cipher::set_aad(std::string const& aad)
    {
        int outlen = 0;
        int ret = EVP_CipherUpdate(ctx, nullptr, &outlen, (unsigned char *) aad.data(), aad.size());
        assert(ret == 1);
        return ret == 1;
    }

    bool EVP_Cipher::update(unsigned char const * data, int len)
    {
        int BLOCK_SIZE = EVP_CIPHER_CTX_block_size(ctx);
        if (remain) {
            if (remain + len < BLOCK_SIZE) {
                memcpy(in + remain, data, len);
                remain += len;
                return true;
            } else {
                memcpy(in + remain, data, BLOCK_SIZE - remain);
                len -= BLOCK_SIZE - remain;
                data += BLOCK_SIZE - remain;
                int outlen = out.size();
                int ret = EVP_CipherUpdate(ctx, out.data(), &outlen, in, BLOCK_SIZE);
                assert(ret == 1);
                if (ret != 1) return false;
                if (outlen > 0 && !output_internal(out.data(), outlen))
                    return false;
                remain = 0;
            }
        }
        while (len >= BLOCK_SIZE) {
            int outlen = out.size();
            int len2 = std::min(len, outlen - 32); len2 = len2 & ~(BLOCK_SIZE - 1);
            int ret = EVP_CipherUpdate(ctx, out.data(), &outlen, data, len2);
            assert(ret == 1);
            assert(outlen <= len2);
            if (ret != 1) return false;
            if (outlen > 0 && !output_internal(out.data(), outlen))
                return false;
            len -= len2;
            data += len2;
        }
        if (len) {
            memcpy(in, data, len);
            remain = len;
        }
        return true;
    }

    bool EVP_Cipher::finalize() {
        int outlen = out.size();
        if (remain) {
            int ret = EVP_CipherUpdate(ctx, out.data(), &outlen, in, remain);
            assert(ret == 1);
            if (ret != 1) return false;
            if (outlen > 0 && !output_internal(out.data(), outlen))
                return false;
        }
        outlen = out.size();
        int ret = EVP_CipherFinal(ctx, out.data(), &outlen);
        // TODO
        int f = flags & (EVP_FLAGS_ENCRYPT | EVP_FLAGS_TAG_SET);
        assert(ret == 1 || f == 0);
        if (ret != 1 && f != 0) return false;
        if (outlen > 0 && !output_internal(out.data(), outlen))
            return false;
        return true;
    }

    bool EVP_Cipher::set_tag(std::string const& tag)
    {
        flags |= EVP_FLAGS_TAG_SET;
        return EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, tag.size(), (unsigned char *) tag.data()) == 1;
    }

    std::string EVP_Cipher::get_tag()
    {
        std::string tag(16, 0);
        int ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, tag.size(), (unsigned char *) tag.data());
        assert (ret == 1);
        if (ret != 1) tag.clear();
        return tag;
    }

    inline bool EVP_Cipher::output_internal(unsigned char const * data, int len)
    {
        if (output_callback)
            return output_callback(output_context, data, len);
        else
            return output(data, len);
    }

    bool EVP_Cipher::output(unsigned char const * data, int len)
    {
        result.append((char *)data, len);
        return true;
    }

    /* DeflateEncrypt */

    DeflateEncrypt::DeflateEncrypt()
    {
        memset(&compressor, 0, sizeof(compressor));
    }

    DeflateEncrypt::~DeflateEncrypt()
    {
    }

    bool DeflateEncrypt::setup_deflate()
    {
        tdefl_status ret = tdefl_init(&compressor, static_deflate_callback, this, tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -15, MZ_DEFAULT_STRATEGY));
        assert(ret == TDEFL_STATUS_OKAY);
        return ret == TDEFL_STATUS_OKAY;
    }

    bool DeflateEncrypt::update(unsigned char const* data, int len)
    {
        if (!compressor.m_pPut_buf_func) {
            return EVP_Cipher::update(data, len);
        }
        tdefl_status ret = tdefl_compress_buffer(&compressor, data, len, TDEFL_NO_FLUSH);
        assert(ret == TDEFL_STATUS_OKAY);
        return ret == TDEFL_STATUS_OKAY;
    }

    bool DeflateEncrypt::finalize()
    {
        if (compressor.m_pPut_buf_func) {
            tdefl_status ret = tdefl_compress_buffer(&compressor, NULL, 0, TDEFL_FINISH);
            assert(ret == TDEFL_STATUS_DONE);
            if (ret != TDEFL_STATUS_DONE) return false;
        }
        return EVP_Cipher::finalize();
    }

    bool DeflateEncrypt::deflate_callback(unsigned char const * data, int len)
    {
        // data from compressor, pass to encrypt
        return EVP_Cipher::update(data, len);
    }

    mz_bool DeflateEncrypt::static_deflate_callback(void const * data, int len, void * thiz)
    {
        if (!((DeflateEncrypt *)thiz)->deflate_callback((unsigned char const *) data, len))
            return 0;
        return len;
    }

    /* DecryptInflate */

    DecryptInflate::DecryptInflate()
    {
        status = TINFL_STATUS_NEEDS_MORE_INPUT;
    }

    DecryptInflate::~DecryptInflate()
    {
    }

    bool DecryptInflate::setup_inflate()
    {
        tinfl_init(&inflator);
        buffer_inflate.resize(TINFL_LZ_DICT_SIZE);
        return true;
    }

    bool DecryptInflate::output_internal(unsigned char const* data, int len)
    {
        // after decrypt
        if (buffer_inflate.empty()) {
            // no need inflate
            return EVP_Cipher::output_internal(data, len);
        }
        if (status != TINFL_STATUS_NEEDS_MORE_INPUT)
            return false;
        // inflate
        do {
            size_t in_buf_size = len;
            mz_uint8 *pWrite_buf_cur = (mz_uint8 *)buffer_inflate.data() + (out_buf_ofs & (TINFL_LZ_DICT_SIZE - 1));
            size_t out_buf_size = TINFL_LZ_DICT_SIZE - (out_buf_ofs & (TINFL_LZ_DICT_SIZE - 1));
            status = tinfl_decompress(&inflator, data, &in_buf_size, (mz_uint8 *)buffer_inflate.data(), pWrite_buf_cur, &out_buf_size, data ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            data += in_buf_size;
            len -= in_buf_size;
            if (status == TINFL_STATUS_NEEDS_MORE_INPUT || status == TINFL_STATUS_HAS_MORE_OUTPUT || status == TINFL_STATUS_DONE) {
                if (out_buf_size) {
                    if (!EVP_Cipher::output_internal(pWrite_buf_cur, out_buf_size))
                        return false;
                    out_buf_ofs += out_buf_size;
                }
            }
            else {
                assert(false);
                status = TINFL_STATUS_FAILED;
                break;
            }
        } while (((status == TINFL_STATUS_NEEDS_MORE_INPUT) || (status == TINFL_STATUS_HAS_MORE_OUTPUT)) && len > 0);
        return len == 0;
    }

    bool DecryptInflate::finalize()
    {
        bool result = EVP_Cipher::finalize();
        if (!buffer_inflate.empty() && !output_internal(nullptr, 0))
            return false;
        return result;
    }

    /* ZipEncrypt */

    ZipEncrypt::ZipEncrypt()
    {
        memset(&output_context, 0, sizeof(mz_zip_writer_staged_context));
    }

    ZipEncrypt::~ZipEncrypt()
    {
        close();
    }

    bool ZipEncrypt::open(mz_zip_writer_staged_context & input_context)
    {
        output_context = input_context;
        output_context.pCompressor->m_pPut_buf_user = &output_context.add_state; // save add state in output_context
        input_context.pCompressor = &compressor;
        this->input_context = &input_context;
        return true;
    }

    bool ZipEncrypt::close()
    {
        if (input_context) { // may not open
            if (!DeflateEncrypt::finalize()) return false;
            output_context.pCompressor->m_pPut_buf_user = &input_context->add_state; // restore add state to input_context
            *input_context = output_context;
            output_context.pCompressor = NULL;
            input_context = NULL;
        }
        return true;
    }

    bool ZipEncrypt::output(unsigned char const* data, int len)
    {
        // data from encrypt, pass to second compressor
        return mz_zip_writer_add_staged_data(&output_context, (char *)data, len);
    }

    /* ZipDecrypt */

    ZipDecrypt::ZipDecrypt()
        : input_state(output_state)
    {
        memset(&output_state, 0, sizeof(mz_zip_reader_extract_iter_state));
    }

    ZipDecrypt::~ZipDecrypt()
    {
        close();
    }

    bool ZipDecrypt::open(mz_uint64 size, mz_file_write_func callback, void * opaque)
    {
        this->size = size;
        this->callback = callback;
        this->opaque = opaque;
        return true;
    }

    bool ZipDecrypt::close()
    {
        if (size) {
            size = 0;
            return DecryptInflate::finalize();
        }
        return true;
    }

    size_t ZipDecrypt::static_callback(void* pOpaque, mz_uint64 file_ofs, const void* pBuf, size_t n)
    {
        ZipDecrypt * decrypt = (ZipDecrypt *) pOpaque;
        decrypt->update((unsigned char *) pBuf, n);
        if (file_ofs + n >= decrypt->size) {
            decrypt->close();
        }
        return n;
    }

    bool ZipDecrypt::output(unsigned char const* data, int len)
    {
        return callback(opaque, 0, data, len) == len;
    }

    /* KeyStore */

    std::vector<KeyStore::Consumer> KeyStore::global_consumers {
        { "abc", "", "0123456789abcdef0123456789abcdef"}, 
        { "test3mf01", "test3mfkek01", base64_decode(
            "MIIEogIBAAKCAQEAubdl5ZV99+wA/1vUZeeM8KQaSQ7dV0W9Vw7PNlXszRdoavwW"
            "4D/e70cajoeJ3TJfarA9zdE3pBVzXsja5VM1axzrPCQn77VvFFTLsMa1lBz3UZck"
            "KK7dAVuoREQCH6042/4UGhvKmVoGq9jt0xMV0CBIgWNgfviE6tuiiezGkoPEJXBb"
            "hg0WXNe6JSxYI3fRkjjPh8fHSla5Jil6L+XrT/n6ehShlLN960tn8suxu1AaXuRv"
            "dimZNxVgK7VQKcYQbfKDfpzEi5Jfd2UKxmuKn/87nrreFYaZCeTjFbadP7FkB8wd"
            "SGGCctsdRfkl/pCBkdLrGsv7Is6jRlW7M0ZoBQIDAQABAoIBAAHH8Pm5K8qXYFES"
            "m+BYTqE2KaxesJ+4Iv81PKZ8P3eeDFnOThfbdPNdfrM0OI2/AGxBAW66XWq86+zS"
            "R0sgt6ft0JG0lQ928XhD8eohlbc0aejF5spfFu5+5we0kUKlgiCV+LJhZtl+pAa8"
            "31cBXVmwHZHkFpZRItEvxwjElQjtp1co+kmCudew4ffpPBPUw7TSuOWuQVjo+d5M"
            "h0xaZzMjjxSornv4LRAm1D4NoCabuCx7jRY2gOgl39nwCWi922vssbEjAUg4+862"
            "Jqe/ted4xIGCk8DP+bwxj3WboLjkM4yp/5AcLGkaovhjupLXru4wDqsWr8wbgwV1"
            "BmzUydcCgYEAvDaO6t58uk0kWVEmlGEueln4AfIUjgjo51qbbb23WsPQTZtlp7N0"
            "/qNNKsWktr0ZPRIdIFcxTprd+gy5LGozQGz41J2lT+9DGsmo3dB2e47r+uKDnNwm"
            "Iegp+4LYFiXGLGDNonn7ESSec4Xj8z8YosVHskr64ptPCOzYzmDCkW8CgYEA/Jqj"
            "wLKOYgBVoUTEZQfMe295VKaKrxtqprYCTHF9J9lysxg2WfIVJByoVnpkmy2EI+Mw"
            "+ubtPrx71Cx413dem/S1aOOIsqJPqdFkc+AERV6ZeT1NWLCgzWoczW/N5ZdneUkW"
            "a0i0B0olAiC9b5zx9HB+p1bm7xEL3zL6OUDPu8sCgYBflkXXOs+Vvn/rbK9vRDva"
            "n765Hj0aNaQze2zcuzFXw4MTJwzlstqESGN0iZQxyq/6uCxatG2yQiziRXv19qm4"
            "2p81PCstAZLPFAPTQ4ApGFj4vfmhvJ0RM1u/BKDB/sU63J8TGWhNOI/Qk/tFGpJk"
            "eFUFU9c/JylomwExLyshuQKBgFd2o+SA7tP4Ea45RVdGEANdYcFxuOtQrujydHFL"
            "im5V2GUyqP8T10YdthvbXSJt7CcQ71CwzMzALpAUpfLVHikZ3gZnYlmX4cWG/yUw"
            "F8p9Kt7T3wgqgEMfzsFDSSOJ/QX9zIlxLwSnI5FNDMqsqQpeOTxv1p5IZLfvyrww"
            "OL1pAoGAM/ZoL7qWenZAzD1Gdzo9HlrxlxBJPnr+ZdYqrJZdo/TwARY8LZu07Vsu"
            "aY1ZAqLlkBARRtypmGj04PGbWWRZ3Pn/M5/FgjGa5M9hVnvLJSBklE7tfKLB4KL5"
            "eMADI7JuelOqfKBxXrp8IlzVlU8Mk0VQRw6hjq1zNKLJtD4EFq4=")}
    };

    KeyStore* KeyStore::create(std::string const& keyid)
    {
        KeyStore* ks = new KeyStore;
        std::string kek;
        for (auto & c : KeyStore::global_consumers) {
            if (c.keyid == keyid) {
                ks->consumers.push_back(c);
                ks->consumers.front().keyvalue.clear(); // TODO: decode
                kek = c.keyvalue;
                break;
            }
        }
        if (ks->consumers.empty() || kek.size() != 32) return nullptr;
        // Random CEK
        std::string cek;
        cek.resize(32, 'a');
        // RAND_bytes((unsigned char *)cek.data(), 32);
        std::string uuid = boost::uuids::to_string(boost::uuids::random_generator()());
        // Encrypt CEK with user KEK
        std::string cipher = cipher_encrypt("aes-256-gcm", kek, cek);
        ks->resourcedatagroups.push_back({uuid, cek});
        ks->resourcedatagroups.front().accessrights.push_back(KeyStore::AccessRight{0, {"http://www.w3.org/2009/xmlenc11#aes256-gcm"}, cipher});
        return ks;
    }

    void KeyStore::save(std::ostream& stream) const
    {
        stream << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        stream << "<keystore xmlns=\"http://schemas.microsoft.com/3dmanufacturing/securecontent/2019/04\" xmlns:ds=\"http://www.w3.org/2000/09/xmldsig#\" xmlns:xenc=\"http://www.w3.org/2001/04/xmlenc#\" UUID=\"" << UUID << "\">\n";
        for (auto & c : consumers) {
            stream << " <consumer consumerid=\"" << c.consumerid;
            if (!c.keyid.empty()) stream << "\" keyid=\"" << c.keyid;
            stream << "\">\n";
            if (!c.keyvalue.empty())
                stream << "  <keyvalue>" << c.keyvalue << "</keyvalue>\n";
            stream << " </consumer>\n";
        }
        for (auto & g : resourcedatagroups) {
            stream << " <resourcedatagroup keyuuid=\"" << g.keyuuid << "\">\n";
            for (auto & a : g.accessrights) {
                stream << "  <accessright consumerindex=\"" << a.consumerindex << "\">\n";
                stream << "   <kekparams wrappingalgorithm=\"" << a.kekparams.wrappingalgorithm;
                if (!a.kekparams.mgfalgorithm.empty()) stream << "\" mgfalgorithm=\"" << a.kekparams.mgfalgorithm;
                if (!a.kekparams.digestmethod.empty()) stream << "\" digestmethod=\"" << a.kekparams.digestmethod;
                stream << "\"/>\n";
                stream << "   <cipherdata>\n    <xenc:CipherValue>" << base64_encode(std::string("%3McF\x00\x00\x00\x0c\x00\x00\x00", 12) + a.cipherdata) << "</xenc:CipherValue>\n   </cipherdata>\n";
                stream << "  </accessright>\n";
            }
            for (auto & d : g.resourcedatas) {
                stream << "  <resourcedata path=\"" << d.path << "\">\n";
                stream << "   <cekparams encryptionalgorithm=\"" << d.cekparams.encryptionalgorithm;
                if (!d.cekparams.compression.empty()) stream << "\" compression=\"" << d.cekparams.compression;
                stream << "\">\n";
                if (!d.cekparams.iv.empty())
                    stream << "    <iv>" << base64_encode(d.cekparams.iv) << "</iv>\n";
                if (!d.cekparams.tag.empty())
                    stream << "    <tag>" << base64_encode(d.cekparams.tag) << "</tag>\n";
                if (!d.cekparams.aad.empty())
                    stream << "    <aad>" << base64_encode(d.cekparams.aad) << "</aad>\n";
                stream << "   </cekparams>\n";
                stream << "  </resourcedata>\n";
            }
            stream << " </resourcedatagroup>\n";
        }
        stream << "</keystore>";
    }

    static std::string algorithm_name(std::string algorithm) {
        size_t n = algorithm.find_last_of('#');
        if (n != std::string::npos)
            algorithm = algorithm.substr(n + 1);
        n = algorithm.find_first_of("123456789");
        if (n != std::string::npos)
            algorithm.insert(n, "-");
        return algorithm;
    }

    bool KeyStore::setup(std::string const& path, EVP_Cipher& cipher, bool encode)
    {
        for (auto& g : resourcedatagroups) {
            for (auto & d : g.resourcedatas) {
                if (d.path == path) {
                    if (g.content_key.empty()) {
                        // decode cek
                        for (auto & a : g.accessrights) {
                            auto iter = std::find(global_consumers.begin(), global_consumers.end(), consumers[a.consumerindex]);
                            if (iter != KeyStore::global_consumers.end()) {
                                g.content_key = cipher_decrypt(algorithm_name(a.kekparams.wrappingalgorithm), iter->keyvalue, a.cipherdata);
                            }
                        }
                    }
                    if (g.content_key.empty()) return false;
                    if (d.cekparams.compression == "deflate") {
                        if (encode) ((DeflateEncrypt &) cipher).setup_deflate();
                        else ((DecryptInflate &) cipher).setup_inflate();
                    }
                    cipher.setup(algorithm_name(d.cekparams.encryptionalgorithm), g.content_key, encode, d.cekparams.iv);
                    if (!d.cekparams.aad.empty())
                        cipher.set_aad(d.cekparams.aad);
                    if (!encode && !d.cekparams.tag.empty())
                        cipher.set_tag(d.cekparams.tag);
                    return true;
                }
            }
        }
        if (!encode)
            return false;
        static boost::mutex mutex;
        for (auto& g : resourcedatagroups) {
            if (g.content_key.empty()) continue;
            std::string iv(12, 0); // for aes gcm, it's 96 bits
            RAND_bytes((unsigned char *)iv.data(), iv.size());
            std::string aad(16, 0); // for aes gcm, it's 128 bits
            RAND_bytes((unsigned char *)aad.data(), aad.size());
            boost::unique_lock l(mutex);
            g.resourcedatas.push_back({path, {"http://www.w3.org/2009/xmlenc11#aes256-gcm", "deflate", iv, aad}});
            if (encode) ((DeflateEncrypt &) cipher).setup_deflate();
            else ((DecryptInflate &) cipher).setup_inflate();
            cipher.setup("aes-256-gcm", g.content_key, encode, iv);
            if (!aad.empty())
                cipher.set_aad(aad);
            return true;
        }
        return false;
    }

    bool KeyStore::finalize(std::string const& path, EVP_Cipher& encrypt)
    {
        for (auto& g : resourcedatagroups) {
            for (auto & d : g.resourcedatas) {
                if (d.path == path) {
                    d.cekparams.tag = encrypt.get_tag();
                    return true;
                }
            }
        }
        return false;
    }

    bool KeyStore::rename(std::string const& path_from, std::string const& path_to)
    {
        for (auto& g : resourcedatagroups) {
            for (auto & d : g.resourcedatas) {
                if (d.path == path_from) {
                    d.path = path_to;
                    return true;
                }
            }
        }
        return false;
    }

    static constexpr const char* KEYSTORE_TAG = "keystore";
    static constexpr const char* CONSUMER_TAG = "consumer";
    static constexpr const char* KEYVALUE_TAG = "keyvalue";
    static constexpr const char* CEKPARAMS_TAG = "cekparams";
    static constexpr const char* IV_TAG = "iv";
    static constexpr const char* TAG_TAG = "tag";
    static constexpr const char* AAD_TAG = "aad";
    static constexpr const char* KEKPARAMS_TAG = "kekparams";
    static constexpr const char* ACCESSRIGHT_TAG = "accessright";
    static constexpr const char* CIPHERDATA_TAG = "cipherdata";
    static constexpr const char* CIPHERVALUE_TAG = "xenc:CipherValue";
    static constexpr const char* RESOURCEDATA_TAG = "resourcedata";
    static constexpr const char* RESOURCEDATAGROUP_TAG = "resourcedatagroup";
    static constexpr const char* UUID_ATTR = "UUID";
    static constexpr const char* CONSUMERID_ATTR = "consumerid";
    static constexpr const char* KEYID_ATTR = "keyid";
    static constexpr const char* KEYUUID_ATTR = "keyuuid";
    static constexpr const char* PATH_ATTR = "path";
    static constexpr const char* ENCRYPTIONALGORITHM_ATTR = "encryptionalgorithm";
    static constexpr const char* COMPRESSION_ATTR = "compression";
    static constexpr const char* CONSUMERINDEX_ATTR = "consumerindex";
    static constexpr const char* WRAPPINGALGORITHM_ATTR = "wrappingalgorithm";
    static constexpr const char* MGFALGORITHM_ATTR = "mgfalgorithm";
    static constexpr const char* DIGESTMETHOD_ATTR = "digestmethod";

    /* KeyStoreLoader */

    struct KeyStoreLoaderImpl : KeyStoreLoader
    {
        KeyStoreLoaderImpl(KeyStore * key_store) : key_store(key_store) {}

        virtual bool handle_start_xml_element(const char* name, const char** attributes) override
        {
            bool res = true;

            if (::strcmp(KEYSTORE_TAG, name) == 0)
                res = handle_start_keystore(attributes);
            else if (::strcmp(CONSUMER_TAG, name) == 0)
                res = handle_start_consumer(attributes);
            else if (::strcmp(RESOURCEDATAGROUP_TAG, name) == 0)
                res = handle_start_resourcedatagroup(attributes);
            else if (::strcmp(ACCESSRIGHT_TAG, name) == 0)
                res = handle_start_accessright(attributes);
            else if (::strcmp(KEKPARAMS_TAG, name) == 0)
                res = handle_start_kekparams(attributes);
            else if (::strcmp(RESOURCEDATA_TAG, name) == 0)
                res = handle_start_resourcedata(attributes);
            else if (::strcmp(CEKPARAMS_TAG, name) == 0)
                res = handle_start_cekparams(attributes);

            characters.clear();
            return res;
        }

        virtual bool handle_xml_characters(char const* s, int len) override
        {
            characters = std::string(s, len);
            return true;
        }
        virtual bool handle_end_xml_element(const char* name) override
        {
            bool res = true;

            if (::strcmp(CIPHERVALUE_TAG, name) == 0)
                res = handle_end_CipherValue();
            else if (::strcmp(KEYVALUE_TAG, name) == 0)
                res = handle_end_keyvalue();
            else if (::strcmp(IV_TAG, name) == 0)
                res = handle_end_iv();
            else if (::strcmp(TAG_TAG, name) == 0)
                res = handle_end_tag();
            else if (::strcmp(AAD_TAG, name) == 0)
                res = handle_end_aad();

            return res;
        }

        bool handle_start_keystore(const char** attributes)
        {
            key_store->UUID = get_attribute_value_string(attributes, UUID_ATTR);
            return true;
        }

        bool handle_start_consumer(const char** attributes)
        {
            KeyStore::Consumer c;
            c.consumerid = get_attribute_value_string(attributes, CONSUMERID_ATTR);
            c.keyid = get_attribute_value_string(attributes, KEYID_ATTR);
            key_store->consumers.push_back(c);
            return true;
        }

        bool handle_start_resourcedatagroup(const char** attributes)
        {
            KeyStore::ResourceDataGroup g;
            g.keyuuid = get_attribute_value_string(attributes, KEYUUID_ATTR);
            key_store->resourcedatagroups.push_back(g);
            return true;
        }

        bool handle_start_accessright(const char** attributes)
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::AccessRight a;
            a.consumerindex = get_attribute_value_int(attributes, CONSUMERINDEX_ATTR);
            g.accessrights.push_back(a);
            return true;
        }

        bool handle_start_kekparams(const char** attributes)
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::AccessRight & a = g.accessrights.back();
            KeyStore::KEKParams & k = a.kekparams;
            k.wrappingalgorithm = get_attribute_value_string(attributes, WRAPPINGALGORITHM_ATTR);
            k.mgfalgorithm = get_attribute_value_string(attributes, MGFALGORITHM_ATTR);
            k.digestmethod = get_attribute_value_string(attributes, DIGESTMETHOD_ATTR);
            return true;
        }

        bool handle_start_resourcedata(const char** attributes)
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::ResourceData r;
            r.path = get_attribute_value_string(attributes, PATH_ATTR);
            g.resourcedatas.push_back(r);
            return true;
        }

        bool handle_start_cekparams(const char** attributes)
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::ResourceData & r = g.resourcedatas.back();
            KeyStore::CEKParams & c = r.cekparams;
            c.encryptionalgorithm = get_attribute_value_string(attributes, ENCRYPTIONALGORITHM_ATTR);
            c.compression = get_attribute_value_string(attributes, COMPRESSION_ATTR);
            return true;
        }

        bool handle_end_keyvalue()
        {
            KeyStore::Consumer & c = key_store->consumers.back();
            c.keyvalue = characters; // only for public key
            return true;
        }

        bool handle_end_iv()
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::ResourceData & r = g.resourcedatas.back();
            KeyStore::CEKParams & c = r.cekparams;
            c.iv = base64_decode(characters);
            return true;
        }

        bool handle_end_tag()
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::ResourceData & r = g.resourcedatas.back();
            KeyStore::CEKParams & c = r.cekparams;
            c.tag = base64_decode(characters);
            return true;
        }

        bool handle_end_aad()
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::ResourceData & r = g.resourcedatas.back();
            KeyStore::CEKParams & c = r.cekparams;
            c.aad = base64_decode(characters);
            return true;
        }

        bool handle_end_CipherValue()
        {
            KeyStore::ResourceDataGroup & g = key_store->resourcedatagroups.back();
            KeyStore::AccessRight & a = g.accessrights.back();
            a.cipherdata = base64_decode(characters);
            if (boost::algorithm::starts_with(a.cipherdata, "%3McF\x00\x00\x00") && a.cipherdata.length() > 12) {
                boost::uint32_t head_len;
                memcpy(&head_len, a.cipherdata.data() + 8, 4);
                if (head_len < a.cipherdata.length())
                    a.cipherdata = a.cipherdata.substr(head_len);
            }
            return true;
        }

        static const char*  get_attribute_value_charptr(const char** attributes, const char* attribute_key)
        {
            if ((attributes == nullptr) || (attribute_key == nullptr))
                return nullptr;
            for (unsigned int a = 0; attributes[a]; a += 2) {
                if (::strcmp(attributes[a], attribute_key) == 0)
                    return attributes[a + 1];
            }
            return nullptr;
        }

        static std::string get_attribute_value_string(const char** attributes, const char* attribute_key)
        {
            const char* text = get_attribute_value_charptr(attributes, attribute_key);
            return (text != nullptr) ? text : "";
        }

        static int get_attribute_value_int(const char** attributes, const char* attribute_key)
        {
            int value = 0;
            if (const char *text = get_attribute_value_charptr(attributes, attribute_key); text != nullptr)
                boost::spirit::qi::parse(text, text + strlen(text), boost::spirit::qi::int_, value);
            return value;
        }

        std::string characters;
        KeyStore* key_store;
    };

    KeyStoreLoader* KeyStoreLoader::create(KeyStore* key_store)
    {
        return new KeyStoreLoaderImpl(key_store);
    }

} // namespace Slic3r
