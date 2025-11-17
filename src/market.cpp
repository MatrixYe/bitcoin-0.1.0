// Copyright (c) 2009 Satoshi Nakamoto
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include "headers.h"

map<uint256, CProduct> mapMyProducts; // 我的产品哈希值到产品对象的映射
map<uint256, CProduct> mapProducts;   // 产品哈希值到产品对象的映射
CCriticalSection cs_mapProducts;      // 产品映射的临界区，用于线程安全访问

// 实现原子在用户间的传播，扩大活跃度的影响范围：
bool AdvertInsert(const CProduct &product) {
  uint256 hash = product.GetHash(); // 获取产品的哈希值
  bool fNew = false;                // 是否为新产品
  bool fUpdated = false;            // 是否为更新产品

  // 插入或查找现有产品
  CRITICAL_BLOCK(cs_mapProducts) { // 对共享产品映射进行临界区保护保证并发安全
    // 尝试插入产品到映射中，如果哈希值已存在则返回已存在的迭代器
    pair<map<uint256, CProduct>::iterator, bool> item =
        mapProducts.insert(make_pair(hash, product));

    // 获取现有条目的引用，用于更新或检查
    CProduct *pproduct = &(*(item.first)).second;
    fNew = item.second; // item.second 为true表示插入了新的条目，item.second
                        // 为false表示已经存在了此条目

    // Update if newer
    if (product.nSequence > pproduct->nSequence) {
      // 若 product.nSequence > pproduct->nSequence ，说明这是更高版本：用赋值
      // *pproduct = product 覆盖旧值，并设置 fUpdated = true
      *pproduct = product;
      fUpdated = true;
    }
  }

  // 最后返回 (fNew || fUpdated)只有“新插入”或“成功更新为更高序列”才算有效变化
  return (fNew || fUpdated);
}
// 删除产品
void AdvertErase(const CProduct &product) {
  uint256 hash = product.GetHash(); // 获取产品的哈希值
  CRITICAL_BLOCK(cs_mapProducts) {
    mapProducts.erase(hash); // 从产品映射中删除该产品
    // NotifyProductDeleted(hash);// 通知产品删除事件（没写）
  }
}

// 工具方法：合并两个已经排序序列的并集，去重并返回新增元素数量
template <typename T> unsigned int Union(T &v1, T &v2) {
  // 举个例子说明一下：
  // 若 v1 = [1, 3, 5] ， v2 = [3, 4] （均已排序）
  // 并集 vUnion = [ 1, 3, 4, 5 ]
  // nAdded = 4 - 3 = 1 ，
  // 返回 1，且更新 v1 为[1, 3, 4, 5]

  T vUnion;
  // 预留容量，并集的最大容量是两个集合的大小之和
  vUnion.reserve(v1.size() + v2.size());
  // 并集计算，自动去重
  set_union(v1.begin(), v1.end(), v2.begin(), v2.end(), back_inserter(vUnion));
  // 新增计算，新加入的原子数量是并集大小减去旧集合大小
  unsigned int nAdded = vUnion.size() - v1.size();
  // 只有当有新增元素的时候，才让v1集合赋值为vUnion,否则赋值没意义
  if (nAdded > 0)
    v1 = vUnion;
  return nAdded; // 并集操作行为造成v1的新增元素个数
}
/**
在用户的信誉原子集合中加入一个新原子，并按照规则决定是否将其用于后续的传播
保持输入原子的去重、有序，控制“从源用户向外传播”的速率与触发条件
- nAtom ：原子编号（ unsigned short ），代表一次活跃/信誉事件
- fOrigin ：是否为“原点原子”（由本用户主动产生的原子，需立即允许传播）
 */
void CUser::AddAtom(unsigned short nAtom, bool fOrigin) {
  // 重复过滤：检查原子是否已存在于vAtomsIn或vAtomsNew中
  // 第一路检测：在已正式集合 vAtomsIn 中用 binary_search 判断是否已存在
  // 第二路检测：在新原子集合 vAtomsNew 中用 find 判断是否已暂存
  if (binary_search(vAtomsIn.begin(), vAtomsIn.end(), nAtom) ||
      find(vAtomsNew.begin(), vAtomsNew.end(), nAtom) != vAtomsNew.end())
    return;

  //// instead of zero atom, should change to free atom that propagates,
  //// limited to lower than a certain value like 5 so conflicts quickly
  // The zero atom never propagates,
  // new atoms always propagate through the user that created them
  // 零原子（0）永远不传播，新原子（非零）总是通过创建它们的用户传播

  // 这里出现了两种原子，命名为零原子、原点原子
  if (nAtom == 0 || fOrigin) {
    // 创建一个临时向量，包含零原子或原点原子，因为Union函数只能接受向量参数
    vector<unsigned short> vTmp(1, nAtom);
    Union(vAtomsIn, vTmp); // 合并零原子或原点原子到 vAtomsIn 中
    if (fOrigin)
      vAtomsOut.push_back(nAtom); // 如果是原点原子，还需要立即传播
    return;
  }
  // 普通原子（非零、非原点）：暂存到 vAtomsNew 中
  vAtomsNew.push_back(nAtom);
  // 如果缓存区普通原子数量超过了流量控制阈值，或者待发送的vAtomsOut
  // 为空，那么执行如下
  if (vAtomsNew.size() >= nFlowthroughRate || vAtomsOut.empty()) {
    // 1、随机选择一个普通原子传播
    vAtomsOut.push_back(vAtomsNew[GetRand(vAtomsNew.size())]);
    // 2、将缓存区普通原子排序然后并合并到 vAtomsIn 中
    sort(vAtomsNew.begin(), vAtomsNew.end());
    Union(vAtomsIn, vAtomsNew); // 合并
    vAtomsNew.clear();          // 3、清空缓存区普通原子
  }
}

// 重要！！原子(atom)数据的传播机制：批量添加原子并传播
// 该函数用于批量添加用户的原子到用户数据库中，并根据规则传播这些原子。
// 它接受起始用户哈希hashUserStart、待传播的原子向量vAtoms、是否为原点原子fOrigin作为参数。
// 函数返回一个布尔值，表示操作是否成功。
bool AddAtomsAndPropagate(uint256 hashUserStart,const vector<unsigned short> &vAtoms, bool fOrigin) {
 // 创建数据库对象，用于读写用户数据                           
  CReviewDB reviewdb;
  // 创建一个数组包含两个map，一个用于处理入向原子，一个用于处理出向原子，交替使用
  map<uint256, vector<unsigned short>> pmapPropagate[2];

  // 初始化第一个map，将起始用户和待传播的原子加入
  pmapPropagate[0][hashUserStart] = vAtoms; 

  // 传播循环：从起始用户开始，根据规则传播原子
  for (int side = 0; !pmapPropagate[side].empty(); side = 1 - side) {
    //在本次循环中，定义mapFrom为当前侧的map，mapTo为另一侧的map
    map<uint256, vector<unsigned short>> &mapFrom = pmapPropagate[side];
    map<uint256, vector<unsigned short>> &mapTo = pmapPropagate[1 - side];

    // 遍历当前侧的map，处理每个用户的原子传播
    for (map<uint256, vector<unsigned short>>::iterator mi = mapFrom.begin();mi != mapFrom.end(); ++mi) {
      const uint256 &hashUser = (*mi).first;                  // 当前用户哈希
      const vector<unsigned short> &vReceived = (*mi).second; // 收到的原子

      ///// this would be a lot easier on the database if it put the new atom at
      /// the beginning of the list,
      ///// so the change would be right next to the vector size.
      // Read user
      CUser user;// 从数据库读取当前用户数据
      reviewdb.ReadUser(hashUser, user);
      unsigned int nIn = user.vAtomsIn.size();// 记录当前用户已正式接收的原子数量
      unsigned int nNew = user.vAtomsNew.size();// 记录当前用户暂存的普通原子数量
      unsigned int nOut = user.vAtomsOut.size();// 记录当前用户待传播的原子数量

      // // 为当前用户添加原子
      foreach (unsigned short nAtom, vReceived)
        user.AddAtom(nAtom, fOrigin);
      fOrigin = false;// 仅仅第一次传播的是原子原点，后续传播的原子都不是原点原子

      // Don't bother writing to disk if no changes
      // 如果当前用户的原子集合没有变化，跳过写入数据库
      if (user.vAtomsIn.size() == nIn && user.vAtomsNew.size() == nNew)
        continue;
      if (user.vAtomsOut.size() > nOut)
        foreach (const uint256 &hash, user.vLinksOut)
          mapTo[hash].insert(mapTo[hash].end(), user.vAtomsOut.begin() + nOut,
                             user.vAtomsOut.end());

      // 更新后的内容写入数据库
      if (!reviewdb.WriteUser(hashUser, user))
        return false;
    }
    mapFrom.clear();
  }
  return true;
}

// 评分的核心处理函数：AcceptReview
// 该函数负责验证评分的合法性、更新数据库中的评分记录、创建用户之间的连接关系，并传播原子。
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
