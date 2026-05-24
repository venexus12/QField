/***************************************************************************
  externalstorage.cpp - ExternalStorage

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

#include "externalstorage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSettings>

#include <qgsapplication.h>
#include <qgsauthmanager.h>

#include <utility>

namespace
{
const QString PENDING_STORES_KEY = QStringLiteral( "/qfield/externalStorage/pendingStores" );
}

ExternalStorage::ExternalStorage( QObject *parent )
  : QObject( parent )
{
}

Qgis::ContentStatus ExternalStorage::status() const
{
  return mFetchedContent ? mFetchedContent->status() : Qgis::ContentStatus::NotStarted;
}

QString ExternalStorage::type() const
{
  return mStorage ? mStorage->type() : QString();
}

void ExternalStorage::setType( const QString &type )
{
  if ( mStorage && mStorage->type() == type )
    return;

  mStorage = QgsApplication::instance()->externalStorageRegistry()->externalStorageFromType( type );
  emit typeChanged();
}

QString ExternalStorage::lastError() const
{
  return mLastError;
}

void ExternalStorage::fetch( const QString &url, const QString &authenticationConfigurationId )
{
  if ( mStorage )
  {
    if ( mFetchedContent )
    {
      disconnect( mFetchedContent.get(), &QgsExternalStorageFetchedContent::fetched, this, &ExternalStorage::contentFetched );
      mFetchedContent->cancel();
      mFetchedContent->deleteLater();
    }

    mFetchedContent.reset( mStorage->fetch( url, authenticationConfigurationId, Qgis::ActionStart::Immediate ) );
    emit statusChanged();
    emit fetchedContentChanged();

    connect( mFetchedContent.get(), &QgsExternalStorageContent::errorOccurred, this, &ExternalStorage::contentErrorOccurred );
    connect( mFetchedContent.get(), &QgsExternalStorageFetchedContent::fetched, this, &ExternalStorage::contentFetched );
  }
}

QString ExternalStorage::fetchedContent() const
{
  return mFetchedContent && mFetchedContent->status() == Qgis::ContentStatus::Finished ? mFetchedContent->filePath() : QString();
}

bool ExternalStorage::isStoring() const
{
  return mStoredContent && mStoredContent->status() == Qgis::ContentStatus::Running;
}

int ExternalStorage::pendingStoreCount() const
{
  return static_cast<int>( pendingStores().size() );
}

void ExternalStorage::store( const QString &filePath, const QString &url, const QString &authenticationConfigurationId, bool queueOnError )
{
  if ( !mStorage )
  {
    return;
  }

  if ( mStoredContent )
  {
    if ( queueOnError )
    {
      addPendingStore( filePath, url, authenticationConfigurationId, type() );
    }
    return;
  }

  mStoreFilePath = filePath;
  mStoreUrl = url;
  mStoreAuthenticationConfigurationId = authenticationConfigurationId;
  mStoreStorageType = type();
  mStoreQueueOnError = queueOnError;
  mLastError.clear();

  mStoredContent.reset( mStorage->store( filePath, url, authenticationConfigurationId ) );
  if ( !mStoredContent )
  {
    if ( queueOnError )
    {
      addPendingStore( filePath, url, authenticationConfigurationId, type() );
    }
    else if ( mRetryingPendingStore )
    {
      mRetryingPendingStore = false;
    }
    mLastError = tr( "Failed to prepare external storage upload." );
    emit lastErrorChanged();
    return;
  }

  connect( mStoredContent.get(), &QgsExternalStorageStoredContent::stored, this, &ExternalStorage::contentStored );
  connect( mStoredContent.get(), &QgsExternalStorageStoredContent::stored, this, &ExternalStorage::storeFinished );
  connect( mStoredContent.get(), &QgsExternalStorageStoredContent::canceled, this, &ExternalStorage::storeFinished );
  connect( mStoredContent.get(), &QgsExternalStorageStoredContent::errorOccurred, this, &ExternalStorage::contentErrorOccurred );
  connect( mStoredContent.get(), &QgsExternalStorageStoredContent::errorOccurred, this, &ExternalStorage::storeFinished );

  mStoredContent->store();
  emit isStoringChanged();
}

void ExternalStorage::retryPendingStores()
{
  if ( mStoredContent )
  {
    return;
  }

  const QJsonArray stores = pendingStores();
  if ( stores.isEmpty() )
  {
    mRetryingPendingStore = false;
    emit pendingStoreCountChanged();
    return;
  }

  const QJsonObject storeRequest = stores.first().toObject();
  const QString storageType = storeRequest.value( QStringLiteral( "storageType" ) ).toString();
  if ( !storageType.isEmpty() )
  {
    setType( storageType );
  }

  if ( !mStorage )
  {
    mRetryingPendingStore = false;
    mLastError = tr( "External storage backend is not available." );
    emit lastErrorChanged();
    return;
  }

  mRetryingPendingStore = true;
  store( storeRequest.value( QStringLiteral( "filePath" ) ).toString(),
         storeRequest.value( QStringLiteral( "url" ) ).toString(),
         storeRequest.value( QStringLiteral( "authenticationConfigurationId" ) ).toString(),
         false );
}

void ExternalStorage::contentFetched()
{
  emit statusChanged();
  emit fetchedContentChanged();
}

void ExternalStorage::contentErrorOccurred( const QString &errorString )
{
  mLastError = errorString;
  emit statusChanged();
  emit lastErrorChanged();
}

void ExternalStorage::contentStored()
{
  if ( mStoredContent && mStoredContent->status() == Qgis::ContentStatus::Finished )
  {
    removePendingStore( mStoreFilePath, mStoreUrl, mStoreAuthenticationConfigurationId );
    emit stored( mStoreFilePath, mStoredContent->url() );
  }
}

void ExternalStorage::storeFinished()
{
  const bool uploadFailed = mStoredContent && mStoredContent->status() != Qgis::ContentStatus::Finished;

  if ( mStoredContent && mStoredContent->status() == Qgis::ContentStatus::Failed && mLastError.isEmpty() )
  {
    mLastError = mStoredContent->errorString();
    emit lastErrorChanged();
  }

  if ( uploadFailed && mStoreQueueOnError )
  {
    addPendingStore( mStoreFilePath, mStoreUrl, mStoreAuthenticationConfigurationId, mStoreStorageType );
  }

  if ( mStoredContent )
  {
    mStoredContent.release()->deleteLater();
  }

  mStoreFilePath.clear();
  mStoreUrl.clear();
  mStoreAuthenticationConfigurationId.clear();
  mStoreStorageType.clear();
  mStoreQueueOnError = true;

  emit isStoringChanged();

  if ( mRetryingPendingStore )
  {
    if ( uploadFailed )
    {
      mRetryingPendingStore = false;
    }
    else
    {
      retryPendingStores();
    }
  }
  else if ( !uploadFailed && pendingStoreCount() > 0 )
  {
    retryPendingStores();
  }
}

QJsonArray ExternalStorage::pendingStores() const
{
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson( QSettings().value( PENDING_STORES_KEY ).toByteArray(), &error );
  if ( error.error != QJsonParseError::NoError || !document.isArray() )
  {
    return QJsonArray();
  }

  return document.array();
}

void ExternalStorage::writePendingStores( const QJsonArray &stores )
{
  QSettings settings;
  if ( stores.isEmpty() )
  {
    settings.remove( PENDING_STORES_KEY );
  }
  else
  {
    settings.setValue( PENDING_STORES_KEY, QString::fromUtf8( QJsonDocument( stores ).toJson( QJsonDocument::Compact ) ) );
  }
  emit pendingStoreCountChanged();
}

void ExternalStorage::addPendingStore( const QString &filePath, const QString &url, const QString &authenticationConfigurationId, const QString &storageType )
{
  QJsonArray stores = pendingStores();
  for ( const QJsonValue &value : std::as_const( stores ) )
  {
    const QJsonObject store = value.toObject();
    if ( store.value( QStringLiteral( "filePath" ) ).toString() == filePath
         && store.value( QStringLiteral( "url" ) ).toString() == url
         && store.value( QStringLiteral( "authenticationConfigurationId" ) ).toString() == authenticationConfigurationId )
    {
      return;
    }
  }

  QJsonObject store;
  store.insert( QStringLiteral( "filePath" ), filePath );
  store.insert( QStringLiteral( "url" ), url );
  store.insert( QStringLiteral( "authenticationConfigurationId" ), authenticationConfigurationId );
  store.insert( QStringLiteral( "storageType" ), storageType );
  stores.append( store );
  writePendingStores( stores );
  emit storeQueued( filePath, url );
}

void ExternalStorage::removePendingStore( const QString &filePath, const QString &url, const QString &authenticationConfigurationId )
{
  QJsonArray stores = pendingStores();
  QJsonArray updatedStores;
  bool removed = false;

  for ( const QJsonValue &value : std::as_const( stores ) )
  {
    const QJsonObject store = value.toObject();
    const bool sameStore = store.value( QStringLiteral( "filePath" ) ).toString() == filePath
                           && store.value( QStringLiteral( "url" ) ).toString() == url
                           && store.value( QStringLiteral( "authenticationConfigurationId" ) ).toString() == authenticationConfigurationId;

    if ( sameStore )
    {
      removed = true;
      continue;
    }

    updatedStores.append( store );
  }

  if ( removed )
  {
    writePendingStores( updatedStores );
  }
}
