// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"

//
// Global state variables
//

map<uint256, CProduct> mapMyProducts;

map<uint256, CProduct> mapProducts;
CCriticalSection cs_mapProducts;

bool AdvertInsert(const CProduct &product) {
  uint256 hash = product.GetHash();
  bool fNew = false;
  bool fUpdated = false;

  CRITICAL_BLOCK(cs_mapProducts) {
    // Insert or find existing product
    pair<map<uint256, CProduct>::iterator, bool> item =
        mapProducts.insert(make_pair(hash, product));
    CProduct *pproduct = &(*(item.first)).second;
    fNew = item.second;

    // Update if newer
    if (product.nSequence > pproduct->nSequence) {
      *pproduct = product;
      fUpdated = true;
    }
  }

  // if (fNew)
  //     NotifyProductAdded(hash);
  // else if (fUpdated)
  //     NotifyProductUpdated(hash);

  return (fNew || fUpdated);
}

void AdvertErase(const CProduct &product) {
  uint256 hash = product.GetHash();
  CRITICAL_BLOCK(cs_mapProducts)
  mapProducts.erase(hash);
  // NotifyProductDeleted(hash);
}

template <typename T> unsigned int Union(T &v1, T &v2) {
  // v1 = v1 union v2
  // v1 and v2 must be sorted
  // returns the number of elements added to v1

  ///// need to check that this is equivalent, then delete this comment
  // vector<unsigned short> vUnion(v1.size() + v2.size());
  // vUnion.erase(set_union(v1.begin(), v1.end(),
  //                        v2.begin(), v2.end(),
  //                        vUnion.begin()),
  //              vUnion.end());

  T vUnion;
  vUnion.reserve(v1.size() + v2.size());
  set_union(v1.begin(), v1.end(), v2.begin(), v2.end(), back_inserter(vUnion));
  unsigned int nAdded = vUnion.size() - v1.size();
  if (nAdded > 0)
    v1 = vUnion;
  return nAdded;
}
// 添加原子的方法
// 原子分为 “原点原子”（用户主动产生，fOrigin=true）和
// “普通原子”（接收自其他用户）。

void CUser::AddAtom(unsigned short nAtom, bool fOrigin) {
  // 忽略重复原子
  if (binary_search(vAtomsIn.begin(), vAtomsIn.end(), nAtom) ||
      find(vAtomsNew.begin(), vAtomsNew.end(), nAtom) != vAtomsNew.end())
    return;

  //// instead of zero atom, should change to free atom that propagates,
  //// limited to lower than a certain value like 5 so conflicts quickly
  // The zero atom never propagates,
  // new atoms always propagate through the user that created them
  // 原点原子（fOrigin=true）或特殊原子（nAtom=0）直接加入vAtomsIn，并可能通过vAtomsOut传播
  if (nAtom == 0 || fOrigin) {
    vector<unsigned short> vTmp(1, nAtom);
    Union(vAtomsIn, vTmp); // 合并到vAtomsIn（去重）
    if (fOrigin)
      vAtomsOut.push_back(nAtom); // 原点原子需传播给关联对象
    return;
  }
  // 普通原子先存入vAtomsNew
  vAtomsNew.push_back(nAtom);
  // 当新原子数量达到流转阈值（nFlowthroughRate=2），或无发出的原子时，触发流转
  if (vAtomsNew.size() >= nFlowthroughRate || vAtomsOut.empty()) {
    // 随机选择一个新原子加入vAtomsOut（用于传播）
    vAtomsOut.push_back(vAtomsNew[GetRand(vAtomsNew.size())]);

    // 将vAtomsNew合并到vAtomsIn（转为正式原子），并清空vAtomsNew
    sort(vAtomsNew.begin(), vAtomsNew.end());
    Union(vAtomsIn, vAtomsNew);
    vAtomsNew.clear();
  }
}
// 实现原子在用户间的传播，扩大活跃度的影响范围：
// AddAtomsAndPropagate函数实现原子在用户间的传播，扩大活跃度的影响范围：
// 每轮传播中，接收原子的用户会更新自己的原子状态，并将新增的原子继续传播给下一级关联对象，形成活跃度的扩散链。
bool AddAtomsAndPropagate(uint256 hashUserStart,
                          const vector<unsigned short> &vAtoms, bool fOrigin) {
  CReviewDB reviewdb;
  map<uint256, vector<unsigned short>> pmapPropagate[2];
  pmapPropagate[0][hashUserStart] = vAtoms; // 起始用户及待传播的原子

  for (int side = 0; !pmapPropagate[side].empty(); side = 1 - side) {
    map<uint256, vector<unsigned short>> &mapFrom = pmapPropagate[side];
    map<uint256, vector<unsigned short>> &mapTo = pmapPropagate[1 - side];

    for (map<uint256, vector<unsigned short>>::iterator mi = mapFrom.begin();
         mi != mapFrom.end(); ++mi) {
      const uint256 &hashUser = (*mi).first;                  // 当前用户哈希
      const vector<unsigned short> &vReceived = (*mi).second; // 收到的原子

      ///// this would be a lot easier on the database if it put the new atom at
      /// the beginning of the list,
      ///// so the change would be right next to the vector size.
      // Read user
      CUser user;
      reviewdb.ReadUser(hashUser, user);
      unsigned int nIn = user.vAtomsIn.size();
      unsigned int nNew = user.vAtomsNew.size();
      unsigned int nOut = user.vAtomsOut.size();

      // // 为当前用户添加原子
      foreach (unsigned short nAtom, vReceived)
        user.AddAtom(nAtom, fOrigin);
      fOrigin = false;

      // Don't bother writing to disk if no changes
      if (user.vAtomsIn.size() == nIn && user.vAtomsNew.size() == nNew)
        continue;
      // 若用户发出的原子数量增加，将新增的原子传播给其关联对象（vLinksOut）
      // 若用户发出的原子数量增加，将新增的原子传播给其关联对象（vLinksOut）
      if (user.vAtomsOut.size() > nOut)
        foreach (const uint256 &hash, user.vLinksOut)
          mapTo[hash].insert(mapTo[hash].end(), user.vAtomsOut.begin() + nOut,
                             user.vAtomsOut.end());

      // // 保存更新后的用户状态 // 保存更新后的用户状态
      if (!reviewdb.WriteUser(hashUser, user))
        return false;
    }
    mapFrom.clear();
  }
  return true;
}
// 评分的核心处理
bool CReview::AcceptReview() {
  // 确定时间戳
  nTime = GetTime();

  // 验证签名
  if (!CKey::Verify(vchPubKeyFrom, GetSigHash(), vchSig))
    return false;
  // 读取数据库中的已有评分
  CReviewDB reviewdb;

  // Add review text to recipient
  vector<CReview> vReviews;
  reviewdb.ReadReviews(hashTo, vReviews);
  vReviews.push_back(*this);
  if (!reviewdb.WriteReviews(hashTo, vReviews))
    return false;

  // 创建一个评分者和被评分者的连接关系
  CUser user;
  uint256 hashFrom = Hash(vchPubKeyFrom.begin(), vchPubKeyFrom.end());
  reviewdb.ReadUser(hashFrom, user);
  user.vLinksOut.push_back(hashTo);
  if (!reviewdb.WriteUser(hashFrom, user))
    return false;

  reviewdb.Close(); // 关闭数据库

  // Propagate atoms to recipient
  vector<unsigned short> vZeroAtom(1, 0);
  if (!AddAtomsAndPropagate(
          hashTo, user.vAtomsOut.size() ? user.vAtomsOut : vZeroAtom, false))
    return false;

  return true;
}

bool CProduct::CheckSignature() {
  return (CKey::Verify(vchPubKeyFrom, GetSigHash(), vchSig));
}

bool CProduct::CheckProduct() {
  if (!CheckSignature())
    return false;

  // Make sure it's a summary product
  if (!mapDetails.empty() || !vOrderForm.empty())
    return false;

  // Look up seller's atom count
  CReviewDB reviewdb("r");
  CUser user;
  reviewdb.ReadUser(GetUserHash(), user);
  nAtoms = user.GetAtomCount();
  reviewdb.Close();

  ////// delme, this is now done by AdvertInsert
  //// Store to memory
  // CRITICAL_BLOCK(cs_mapProducts)
  //     mapProducts[GetHash()] = *this;

  return true;
}
