#pragma once

/// @file CertGenerator.hpp
/// @brief Dynamic X.509 certificate generation for TLS MITM interception.
///
/// ## How MITM Certificate Forging Works
///
/// When the proxy intercepts an HTTPS connection, it needs to present a
/// certificate to the client that:
/// 1. Has the same CN (Common Name) as the real server's cert
/// 2. Has matching SAN (Subject Alternative Name) DNS entries
/// 3. Is signed by a CA that the client trusts
///
/// The proxy operator generates a local CA (via tools/gen_ca.sh) and installs
/// it in the system trust store. CertGenerator loads this CA and dynamically
/// creates leaf certificates on-the-fly for each intercepted hostname.
///
/// ## Generated Certificate Properties
///
/// - RSA 2048-bit key pair (per leaf cert)
/// - SHA-256 signature (signed by the loaded CA)
/// - Random serial number
/// - Validity: backdated 1 day, valid for 180 days
/// - SAN: DNS:<hostname>
/// - Basic Constraints: CA:FALSE
///
/// ## OpenSSL Types
///
/// We use RAII wrappers (X509Ptr, EVPKeyPtr) with custom deleters to ensure
/// OpenSSL objects are freed correctly. These are move-only unique_ptrs.
///
/// ## Thread Safety
///
/// CertGenerator is NOT thread-safe. Each worker thread should either:
/// - Have its own CertGenerator instance, or
/// - Access a shared one through the CertCache (which is mutex-protected)

#include <DeepSeer/Core/Types.hpp>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <memory>
#include <string>
#include <string_view>

namespace DeepSeer
{

struct X509Deleter
{
    void operator()(X509* p) const { X509_free(p); }
};

struct EVPKeyDeleter
{
    void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); }
};

using X509Ptr   = std::unique_ptr<X509, X509Deleter>;
using EVPKeyPtr = std::unique_ptr<EVP_PKEY, EVPKeyDeleter>;

/// A certificate + private key pair. Returned by CertGenerator::generate().
struct CertKeyPair
{
    X509Ptr cert;
    EVPKeyPtr key;
};

/// Generates forged TLS certificates signed by a local CA.
class CertGenerator
{
public:
    /// Load the CA certificate and private key from PEM files.
    /// @param caCertPath Path to the CA certificate (e.g., ca.crt)
    /// @param caKeyPath  Path to the CA private key (e.g., ca.key)
    static Expected<CertGenerator> create(std::string const& caCertPath,
                                          std::string const& caKeyPath);

    /// Generate a leaf certificate for the given hostname.
    /// Creates a new RSA 2048 key, sets CN and SAN to the hostname,
    /// and signs with the loaded CA key.
    Expected<CertKeyPair> generate(std::string_view hostname);

    X509*     caCert() const { return caCert_.get(); }
    EVP_PKEY* caKey() const { return caKey_.get(); }

private:
    CertGenerator(X509Ptr ca_cert, EVPKeyPtr ca_key);

    X509Ptr caCert_;
    EVPKeyPtr caKey_;
};

} // namespace DeepSeer
