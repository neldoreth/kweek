#include "credentialstore.h"

#include <QMap>

#include <KWallet>

namespace
{
const QString kFolder = QStringLiteral("Kweek");
}

bool CredentialStore::storeCredentials(const QString &accountId, const QString &username, const QString &password)
{
    KWallet::Wallet *wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet) {
        return false;
    }

    if (!wallet->hasFolder(kFolder)) {
        wallet->createFolder(kFolder);
    }
    wallet->setFolder(kFolder);

    QMap<QString, QString> map;
    map.insert(QStringLiteral("username"), username);
    map.insert(QStringLiteral("password"), password);

    const int result = wallet->writeMap(accountId, map);
    delete wallet;
    return result == 0;
}

bool CredentialStore::readCredentials(const QString &accountId, QString &username, QString &password)
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

    QMap<QString, QString> map;
    const int result = wallet->readMap(accountId, map);
    delete wallet;

    if (result != 0) {
        return false;
    }

    username = map.value(QStringLiteral("username"));
    password = map.value(QStringLiteral("password"));
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
    }
    delete wallet;
    return true;
}
