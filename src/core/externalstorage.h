/***************************************************************************
  externalstorage.h - ExternalStorage

 ---------------------
 begin                : 07.04.2025
 copyright            : (C) 2025 by Mathieu Pellerin
 email                : mathieu@opengis.ch
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef EXTERNALSTORAGE_H
#define EXTERNALSTORAGE_H

#include <QJsonArray>
#include <QObject>
#include <qgsexternalstorage.h>
#include <qgsexternalstorageregistry.h>

#include <memory>

class QNetworkReply;

/**
 * \ingroup core
 */
class ExternalStorage : public QObject
{
    Q_OBJECT

    Q_PROPERTY( Qgis::ContentStatus status READ status NOTIFY statusChanged )
    Q_PROPERTY( QString type READ type WRITE setType NOTIFY typeChanged )
    Q_PROPERTY( QString lastError READ lastError NOTIFY lastErrorChanged )

    Q_PROPERTY( QString fetchedContent READ fetchedContent NOTIFY fetchedContentChanged )
    Q_PROPERTY( bool isStoring READ isStoring NOTIFY isStoringChanged )
    Q_PROPERTY( int pendingStoreCount READ pendingStoreCount NOTIFY pendingStoreCountChanged )

  public:
    explicit ExternalStorage( QObject *parent = nullptr );

    /**
     * Returns the current status of the external storage object. When a fetch operation has been triggered,
     * the status will reflect the last fetched content operation.
     */
    Qgis::ContentStatus status() const;

    /**
     * Returns the current external storage type string.
     */
    QString type() const;

    /**
     * Sets the current external storage type string. The type string must be tied to an
     * external storage object that was added in the QgsApplication::externalStorageRegistry().
     */
    void setType( const QString &type );

    /**
     * Returns the last error emitted by an external storage operation.
     */
    QString lastError() const;

    /**
     * Returns the file path of a successfully fetched content operation.
     */
    QString fetchedContent() const;

    /**
     * Returns TRUE if a content store operation is currently running.
     */
    bool isStoring() const;

    /**
     * Returns the number of queued external storage upload requests.
     */
    int pendingStoreCount() const;

    /**
     * Triggers a fetch operation to download the content from an external storage and
     * make it available locally.
     * \param url the remote URL of the content
     * \param authenticationConfigurationId the authentication configuration ID used to
     * connect to the external storage endpoint
     */
    Q_INVOKABLE void fetch( const QString &url, const QString &authenticationConfigurationId );

    /**
     * Triggers a store operation to upload local content into external storage.
     * \param filePath the local file path of the content to store
     * \param url the remote URL where the content should be stored
     * \param authenticationConfigurationId the authentication configuration ID used to
     * connect to the external storage endpoint
     */
    Q_INVOKABLE void store( const QString &filePath, const QString &url, const QString &authenticationConfigurationId, bool queueOnError = true );

    /**
     * Retries queued external storage uploads. Uploading stops at the first failure,
     * leaving the item queued for a later retry.
     */
    Q_INVOKABLE void retryPendingStores();

  signals:
    void statusChanged();
    void typeChanged();
    void fetchedContentChanged();
    void lastErrorChanged();
    void isStoringChanged();
    void pendingStoreCountChanged();
    void stored( const QString &filePath, const QString &url );
    void storeQueued( const QString &filePath, const QString &url );

  private slots:
    void contentFetched();
    void contentErrorOccurred( const QString &errorString );
    void storeErrorOccurred( const QString &errorString );
    void contentStored();
    void storeFinished();

  private:
    QJsonArray pendingStores() const;
    void writePendingStores( const QJsonArray &stores );
    void addPendingStore( const QString &filePath, const QString &url, const QString &authenticationConfigurationId, const QString &storageType );
    void removePendingStore( const QString &filePath, const QString &url, const QString &authenticationConfigurationId );
    bool canUseDirectWebdavStore() const;
    bool startDirectWebdavStore();
    void directWebdavStoreFinished( QNetworkReply *reply );
    void finishStore( bool uploadFailed );

    Qgis::ContentStatus mStatus = Qgis::ContentStatus::NotStarted;
    QString mType;
    QgsExternalStorage *mStorage = nullptr;
    QString mLastError;

    QString mFetchUrl;
    std::unique_ptr<QgsExternalStorageFetchedContent> mFetchedContent;
    QString mStoreFilePath;
    QString mStoreUrl;
    QString mStoreAuthenticationConfigurationId;
    QString mStoreStorageType;
    bool mStoreQueueOnError = true;
    bool mRetryingPendingStore = false;
    std::unique_ptr<QgsExternalStorageStoredContent> mStoredContent;
    QNetworkReply *mDirectStoreReply = nullptr;
};

#endif // EXTERNALSTORAGE_H
