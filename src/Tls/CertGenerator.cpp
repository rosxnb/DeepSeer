#include <DeepSeer/Tls/CertGenerator.hpp>

#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>

namespace DeepSeer
{

CertGenerator::CertGenerator(X509Ptr caCert, EVPKeyPtr caKey)
    : caCert_(std::move(caCert)), caKey_(std::move(caKey))
{ }

Expected<CertGenerator>
CertGenerator::create(std::string const& caCertPath,
                      std::string const& caKeyPath)
{
    // Load CA certificate
    FILE* certFp = fopen(caCertPath.c_str(), "r");
    if (!certFp) {
        return std::unexpected(
            Error{ ErrorCode::ConfigError, "Cannot open CA cert: " + caCertPath });
    }

    X509* rawCert = PEM_read_X509(certFp, nullptr, nullptr, nullptr);
    fclose(certFp);
    if (!rawCert) {
        return std::unexpected(Error{ ErrorCode::ConfigError, "Failed to parse CA cert" });
    }

    // Load CA private key
    FILE* keyFp = fopen(caKeyPath.c_str(), "r");
    if (!keyFp) {
        X509_free(rawCert);
        return std::unexpected(
            Error{ ErrorCode::ConfigError, "Cannot open CA key: " + caKeyPath });
    }

    EVP_PKEY* rawKey = PEM_read_PrivateKey(keyFp, nullptr, nullptr, nullptr);
    fclose(keyFp);
    if (!rawKey) {
        X509_free(rawCert);
        return std::unexpected(Error{ ErrorCode::ConfigError, "Failed to parse CA key" });
    }

    return CertGenerator(X509Ptr(rawCert), EVPKeyPtr(rawKey));
}

Expected<CertKeyPair>
CertGenerator::generate(std::string_view hostname)
{
    // Generate a new RSA key pair for the leaf cert
    EVPKeyPtr leafKey{EVP_RSA_gen(2048)};
    if (!leafKey) {
        return std::unexpected(Error{ ErrorCode::CertGenerationFailed, "Failed to generate RSA key" });
    }

    // Create the X509 certificate
    X509Ptr cert{X509_new()};
    if (!cert) {
        return std::unexpected(Error{ ErrorCode::CertGenerationFailed, "Failed to create X509" });
    }

    // Version 3
    X509_set_version(cert.get(), 2);

    // Random serial number
    std::random_device rd;
    std::mt19937_64 gen{rd()};
    auto serial = static_cast<long>(gen() & 0x7FFFFFFF);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial);

    // Validity: backdate 1 day, valid for 180 days
    X509_gmtime_adj(X509_get_notBefore(cert.get()), -86400);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), 180 * 86400);

    // Set subject CN to hostname
    X509_NAME* name = X509_get_subject_name(cert.get());
    std::string cn{hostname};
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);

    // Set issuer to CA's subject
    X509_set_issuer_name(cert.get(), X509_get_subject_name(caCert_.get()));

    // Set public key
    X509_set_pubkey(cert.get(), leafKey.get());

    // Add Subject Alternative Name extension
    std::string sanValue = "DNS:" + cn;
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, caCert_.get(), cert.get(), nullptr, nullptr, 0);

    X509_EXTENSION* sanExt =
        X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name,
                            const_cast<char*>(sanValue.c_str()));
    if (sanExt) {
        X509_add_ext(cert.get(), sanExt, -1);
        X509_EXTENSION_free(sanExt);
    }

    // Add Basic Constraints: CA:FALSE
    X509_EXTENSION* bcExt =
        X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, const_cast<char*>("CA:FALSE"));
    if (bcExt) {
        X509_add_ext(cert.get(), bcExt, -1);
        X509_EXTENSION_free(bcExt);
    }

    // Sign with CA's private key
    if (!X509_sign(cert.get(), caKey_.get(), EVP_sha256())) {
        return std::unexpected(Error{ ErrorCode::CertGenerationFailed, "Failed to sign certificate" });
    }

    return CertKeyPair{ std::move(cert), std::move(leafKey) };
}

} // namespace DeepSeer
