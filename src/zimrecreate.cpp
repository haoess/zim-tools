/*
 * Copyright (C) 2013 Kiran Mathew Koshy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU  General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <iostream>
#include <sstream>
#include <vector>
#include <zim/writer/creator.h>
#include <zim/blob.h>
#include <zim/article.h>
#include <zim/file.h>
#include <zim/fileiterator.h>
#include <list>
#include <algorithm>
#include <sstream>

#include "version.h"

class Article : public zim::writer::Article         //Article class that will be passed to the zimwriter. Contains a zim::Article class, so it is easier to add a
{
    zim::Article Ar;
public:
    explicit Article(const zim::Article a):
      Ar(a)
    {}

    virtual zim::writer::Url getUrl() const
    {
        return zim::writer::Url(Ar.getNamespace(), Ar.getUrl());
    }

    virtual std::string getTitle() const
    {
        return Ar.getTitle();
    }

    virtual bool isRedirect() const
    {
        return Ar.isRedirect();
    }

    virtual std::string getMimeType() const
    {
        if (isRedirect()) { return ""; }
        return Ar.getMimeType();
    }

    virtual zim::writer::Url getRedirectUrl() const {
      auto redirectArticle = Ar.getRedirectArticle();
      return zim::writer::Url(redirectArticle.getNamespace(), redirectArticle.getUrl());
    }

    virtual std::string getParameter() const
    {
        return Ar.getParameter();
    }

    zim::Blob getData() const
    {
        return Ar.getData();
    }

    zim::size_type getSize() const
    {
        return Ar.getArticleSize();
    }

    std::string getFilename() const
    {
        return "";
    }

    bool shouldCompress() const
    {
        return getMimeType().find("text") == 0
            || getMimeType() == "application/javascript"
            || getMimeType() == "application/json"
            || getMimeType() == "image/svg+xml";
    }

    bool shouldIndex() const
    {
        return getMimeType().find("text/html") == 0;
    }
};

using pair_type = std::pair<zim::article_index_type, zim::cluster_index_type>;

class ComparatorByCluster {
  public:
    ComparatorByCluster(const zim::File& origin):
      origin(origin) {
    }

    bool operator() (pair_type i, pair_type j) {
      return i.second < j.second;
    }
  const zim::File& origin;
};


class ZimRecreator : public zim::writer::Creator
{
    zim::File origin;

public:
  explicit ZimRecreator(std::string originFilename="", bool zstd = false) :
    zim::writer::Creator(true, zstd ? zim::zimcompZstd : zim::zimcompLzma)
    {
        origin = zim::File(originFilename);
        // [TODO] Use the correct language
        setIndexing(true, "eng");
        setMinChunkSize(2048);
    }

    virtual void create(const std::string& fname)
    {
        std::cout << "generate list of articles" << std::endl;
        std::vector<pair_type> article_list;
        auto nb_articles = origin.getCountArticles();
        article_list.reserve(nb_articles);
        for(zim::article_index_type i=0; i<nb_articles; i++) {
            auto article = origin.getArticle(i);
            article_list.push_back(std::make_pair(i, article.getClusterNumber()));
            if ((i % 10000) == 0)
              std::cout << i << "/" << nb_articles << std::endl;
        }
        std::cout << "sorting articles" << std::endl;
        {
          ComparatorByCluster comparator(origin);
          std::sort(article_list.begin(), article_list.end(), comparator);
        }
        std::cout << "starting zim creation" << std::endl;
        startZimCreation(fname);
        for(auto& pair: article_list)
        {
          auto article = origin.getArticle(pair.first);
          if (article.getNamespace() == 'Z' || article.getNamespace() == 'X') {
            // Index is recreated by zimCreator. Do not add it
            continue;
          }
          auto tempArticle = std::make_shared<Article>(article);
          addArticle(tempArticle);
        }
        finishZimCreation();
    }

    virtual zim::writer::Url getMainUrl() const {
      if (!origin.getFileheader().hasMainPage()) {
        return zim::writer::Url();
      }
      auto mainArticle = origin.getArticle(origin.getFileheader().getMainPage());
      return zim::writer::Url(mainArticle.getNamespace(), mainArticle.getUrl());
    }

    virtual zim::writer::Url getLayoutUrl() const {
      if (!origin.getFileheader().hasLayoutPage()) {
       return zim::writer::Url();
      }
      auto layoutArticle = origin.getArticle(origin.getFileheader().getLayoutPage());
      return zim::writer::Url(layoutArticle.getNamespace(), layoutArticle.getUrl());
    }
};

void usage()
{
    std::cout << "\nzimrecreate recreates a ZIM file from a existing ZIM.\n"
    "\nUsage: zimrecreate ORIGIN_FILE OUTPUT_FILE [Options]"
    "\nOptions:\n"
    "\t-v, --version    print software version\n"
    "\t-z, --zstd       use Zstandard as ZIM compression (lzma otherwise)\n";
    return;
}

int main(int argc, char* argv[])
{
    bool zstdFlag = false;

    //Parsing arguments
    //There will be only two arguments, so no detailed parsing is required.
    std::cout<<"zimrecreate\n";
    for(int i=0;i<argc;i++)
    {
        if(std::string(argv[i])=="-H" ||
           std::string(argv[i])=="--help" ||
           std::string(argv[i])=="-h")
        {
            usage();
            return 0;
        }

        if(std::string(argv[i])=="--version" ||
           std::string(argv[i])=="-v")
        {
            version();
            return 0;
        }

        if(std::string(argv[i])=="--zstd" ||
           std::string(argv[i])=="-z")
        {
            zstdFlag = true;
        }
    }
    if(argc<3)
    {
        std::cout<<"\n[ERROR] Not enough Arguments provided\n";
        usage();
        return -1;
    }
    std::string originFilename =argv[1];
    std::string outputFilename =argv[2];
    try
    {
        ZimRecreator c(originFilename, zstdFlag);
        //Create the actual file.
        c.create(outputFilename);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }
}
