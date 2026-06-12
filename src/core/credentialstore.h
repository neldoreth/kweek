#pragma once

#include <QString>

/**
 * Thin synchronous wrapper around KWallet for storing CalDAV account
 * credentials (username/password), keyed by account id.
 */
namespace CredentialStore
{

/// Stores (overwriting any existing) credentials for the given account id.
bool storeCredentials(const QString &accountId, const QString &username, const QString &password);

/// Reads back previously stored credentials. Returns false if not found.
bool readCredentials(const QString &accountId, QString &username, QString &password);

/// Removes stored credentials for the given account id.
bool removeCredentials(const QString &accountId);

}
