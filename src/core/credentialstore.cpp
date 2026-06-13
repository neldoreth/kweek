#include "credentialstore.h"

#include <QMap>

#include <KWallet>

namespace
{
const QString kFolder = QStringLiteral("Kweek");

bool writeMap(const QString &key, const QMap<QString, QString> &map)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return false;
    }

    if (!wallet->hasFolder(kFolder)) {
        wallet->createFolder(kFolder);
    }
    wallet->setFolder(kFolder);

    const int result = wallet->writeMap(key, map);
    delete wallet;
    return result == 0;
}

bool readMap(const QString &key, QMap<QString, QString> &map)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return false;
    }

    if (!wallet->hasFolder(kFolder)) {
        delete wallet;
        return false;
    }
    wallet->setFolder(kFolder);

    if (!wallet->hasEntry(key)) {
        delete wallet;
        return false;
    }

    const int result = wallet->readMap(key, map);
    delete wallet;
    return result == 0;
}

}

bool CredentialStore::storeCredentials(const QString &accountId, const QString &username, const QString &password)
{
    return writeMap(accountId, {
        {QStringLiteral("username"), username},
        {QStringLiteral("password"), password},
    });
}

bool CredentialStore::readCredentials(const QString &accountId, QString &username, QString &password)
{
    QMap<QString, QString> map;
    if (!readMap(accountId, map)) {
        return false;
    }

    username = map.value(QStringLiteral("username"));
    password = map.value(QStringLiteral("password"));
    return true;
}

bool CredentialStore::storeGoogleTokens(const QString &accountId, const QString &email, const QString &refreshToken)
{
    return writeMap(accountId, {
        {QStringLiteral("email"), email},
        {QStringLiteral("refreshToken"), refreshToken},
    });
}

bool CredentialStore::readGoogleTokens(const QString &accountId, QString &email, QString &refreshToken)
{
    QMap<QString, QString> map;
    if (!readMap(accountId, map)) {
        return false;
    }

    email = map.value(QStringLiteral("email"));
    refreshToken = map.value(QStringLiteral("refreshToken"));
    return true;
}

bool CredentialStore::removeCredentials(const QString &accountId)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return false;
    }

    if (wallet->hasFolder(kFolder)) {
        wallet->setFolder(kFolder);
        wallet->removeEntry(accountId);
        wallet->sync();
    }
    delete wallet;
    return true;
}
