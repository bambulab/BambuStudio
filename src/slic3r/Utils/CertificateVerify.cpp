#include "CertificateVerify.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>
#include <cstring>
#include <iostream>
#include <boost/log/trivial.hpp>

namespace Slic3r {

#if defined(_WIN32)

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <wincrypt.h>

    static std::wstring utf8_to_w(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    static std::wstring wself_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::wstring(buf, buf + n);
    }

    static bool sha256_win(const BYTE* data, DWORD len, std::array<uint8_t, 32>& out) {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return false;
        bool ok = false;
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
            if (CryptHashData(hHash, data, len, 0)) {
                DWORD cb = (DWORD)out.size();
                ok = (CryptGetHashParam(hHash, HP_HASHVAL, out.data(), &cb, 0) && cb == out.size());
            }
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
        return ok;
    }

    static std::string wide_to_utf8(const std::wstring& w) {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    static std::optional<SignerSummary> summarize_file_win(const std::wstring& path) {
        HCERTSTORE hStore = nullptr;
        HCRYPTMSG  hMsg = nullptr;

        if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE,
            path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0, nullptr, nullptr, nullptr,
            &hStore, &hMsg, nullptr)) {
            return std::nullopt;
        }

        DWORD cb = 0;
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &cb) || cb == 0) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }
        std::vector<BYTE> v(cb);
        if (!CryptMsgGetParam(hMsg, CMSG_SIGNER_INFO_PARAM, 0, v.data(), &cb)) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }
        auto* si = reinterpret_cast<PCMSG_SIGNER_INFO>(v.data());

        CERT_INFO ci{};
        ci.Issuer = si->Issuer;
        ci.SerialNumber = si->SerialNumber;

        PCCERT_CONTEXT pCert = CertFindCertificateInStore(
            hStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
            CERT_FIND_SUBJECT_CERT, &ci, nullptr);

        if (!pCert) {
            CryptMsgClose(hMsg); CertCloseStore(hStore, 0);
            return std::nullopt;
        }

        SignerSummary s{};

        // 1) cert_sha256
        DWORD cbHash = (DWORD)s.cert_sha256.size();
        if (!CertGetCertificateContextProperty(
            pCert, CERT_SHA256_HASH_PROP_ID,
            s.cert_sha256.data(), &cbHash) || cbHash != s.cert_sha256.size()) {
            sha256_win(pCert->pbCertEncoded, pCert->cbCertEncoded, s.cert_sha256);
        }

        // 2) spki_sha256 SPKI -> DER -> hash
        DWORD cbSpki = 0;
        if (!CryptEncodeObjectEx(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
            &pCert->pCertInfo->SubjectPublicKeyInfo,
            0, nullptr, nullptr, &cbSpki)) {
            return std::nullopt;
        }

        std::vector<BYTE> spki(cbSpki);
        if (!CryptEncodeObjectEx(
            X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
            &pCert->pCertInfo->SubjectPublicKeyInfo,
            0, nullptr, spki.data(), &cbSpki)) {
            return std::nullopt;
        }

        sha256_win(spki.data(), cbSpki, s.spki_sha256);

        // 3) subject/issuer
        {
            wchar_t buf[1024];
            DWORD n1 = CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, buf, 1024);
            if (n1 > 0) s.subject_dn = wide_to_utf8(std::wstring(buf, buf + (n1 ? n1 - 1 : 0)));
            DWORD n2 = CertGetNameStringW(pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, CERT_NAME_ISSUER_FLAG,
                nullptr, buf, 1024);
            if (n2 > 0) s.issuer_dn = wide_to_utf8(std::wstring(buf, buf + (n2 ? n2 - 1 : 0)));
        }

        CertFreeCertificateContext(pCert);
        CryptMsgClose(hMsg);
        CertCloseStore(hStore, 0);
        return s;
    }

    std::optional<SignerSummary> SummarizeSelf() {
        try {
            return summarize_file_win(wself_path());
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << e.what();
            return std::nullopt;
        }
    }

    std::optional<SignerSummary> SummarizeModule(const std::string& path_utf8) {
        try {
            return summarize_file_win(utf8_to_w(path_utf8));
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << e.what();
            return std::nullopt;
        }
    }

#elif defined(__APPLE__)

#include <cstring>
#include <TargetConditionals.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <CommonCrypto/CommonCrypto.h>

    static bool sha256_mac(const void* data, size_t len, std::array<uint8_t, 32>& out) {
        CC_SHA256_CTX ctx; CC_SHA256_Init(&ctx);
        CC_SHA256_Update(&ctx, data, (CC_LONG)len);
        CC_SHA256_Final(out.data(), &ctx);
        return true;
    }

    static std::string cfstr(CFStringRef s) {
        if (!s) return {};
        CFIndex len = CFStringGetLength(s);
        CFIndex max = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8);
        std::string out(static_cast<size_t>(max) + 1, '\0');
        if (CFStringGetCString(s, out.data(), max, kCFStringEncodingUTF8)) {
            out.resize(strlen(out.c_str()));
            return out;
        }
        return {};
    }

    static std::string dn_from_cert(SecCertificateRef cert) {
        CFStringRef s = SecCertificateCopySubjectSummary(cert);
        std::string out = cfstr(s);
        if (s) CFRelease(s);
        return out;
    }

    static bool fill_from_static_code(SecStaticCodeRef code, SignerSummary& s) {
        CFDictionaryRef info = nullptr;
        auto            copy_inf_ret = SecCodeCopySigningInformation(code, kSecCSSigningInformation | kSecCSRequirementInformation | kSecCSInternalInformation, &info);
        if (copy_inf_ret != errSecSuccess) {
            return false;
        }
        if (!info) {
            return false;
        }

        if (auto tid = (CFStringRef)CFDictionaryGetValue(info, kSecCodeInfoTeamIdentifier)) {
            s.team_id = cfstr(tid);
        }

        // cert chain
        CFArrayRef certs = (CFArrayRef)CFDictionaryGetValue(info, kSecCodeInfoCertificates);
        if (!certs || CFArrayGetCount(certs) == 0) {
            CFRelease(info);
            return false;
        }
        SecCertificateRef leaf = (SecCertificateRef)CFArrayGetValueAtIndex(certs, 0);

        // 1) cert_sha256
        if (auto der = SecCertificateCopyData(leaf)) {
            sha256_mac(CFDataGetBytePtr(der), CFDataGetLength(der), s.cert_sha256);
            CFRelease(der);
        }

        // 2) spki_sha256
        if (auto pk = SecCertificateCopyKey(leaf)) {
            CFDataRef raw = SecKeyCopyExternalRepresentation(pk, nullptr);
            if (raw) {
                sha256_mac(CFDataGetBytePtr(raw), CFDataGetLength(raw), s.spki_sha256);
                CFRelease(raw);
            }
            CFRelease(pk);
        }

        // 3) subject / issuer
        s.subject_dn = dn_from_cert(leaf);
        if (CFArrayGetCount(certs) >= 2) {
            SecCertificateRef iss = (SecCertificateRef)CFArrayGetValueAtIndex(certs, 1);
            s.issuer_dn = dn_from_cert(iss);
        }

        CFRelease(info);
        return true;
    }

    std::optional<SignerSummary> SummarizeSelf() {
        try {
            SecCodeRef dyn = nullptr;
            auto       copy_ret = SecCodeCopySelf(kSecCSDefaultFlags, &dyn);
            if (copy_ret != errSecSuccess) {
                return std::nullopt;
            }
            if (!dyn) {
                return std::nullopt;
            }
            SecStaticCodeRef st = nullptr;
            auto copy_static_ret = SecCodeCopyStaticCode(dyn, kSecCSDefaultFlags, &st);
            CFRelease(dyn);
            if (!st) {
                return std::nullopt;
            }

            SignerSummary s{};
            bool          ok = fill_from_static_code(st, s);
            CFRelease(st);
            return ok ? std::optional<SignerSummary>(s) : std::nullopt;
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << e.what();
            return std::nullopt;
        }
    }

    std::optional<SignerSummary> SummarizeModule(const std::string& path_utf8) {
        try {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *) path_utf8.c_str(), path_utf8.size(), false);
            if (!url) return std::nullopt;

            SecStaticCodeRef code = nullptr;
            OSStatus         st   = SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &code);
            CFRelease(url);
            if (st != errSecSuccess || !code) return std::nullopt;

            SignerSummary s{};
            bool          ok = fill_from_static_code(code, s);
            CFRelease(code);
            return ok ? std::optional<SignerSummary>(s) : std::nullopt;
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << e.what();
            return std::nullopt;
        }
    }

#else
    std::optional<SignerSummary> SummarizeSelf() { return std::nullopt; }
    std::optional<SignerSummary> SummarizeModule(const std::string&) { return std::nullopt; }
#endif

    bool IsSamePublisher(const SignerSummary& a, const SignerSummary& b)
    {
        if (!a.team_id.empty() && a.team_id == b.team_id) return true;
        if (a.spki_sha256 == b.spki_sha256) return true;
        if (a.cert_sha256 == b.cert_sha256) return true;
        return false;
    }
}