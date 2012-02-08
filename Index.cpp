#include "TreeNode.h"
#include "FileHandler.h"
#include "Utils.h"

int keylen(KeyType *keytype){
	int len=0;
	for (int i=0;i<keytype->numAttrs; i++) {
		len += keytype->attrLen[i];
	}
	return len;
}


class Index{
public:
	FileHandler *fHandler;
	TreeNode *root;
	char rootAddress[NODE_OFFSET_SIZE];
	KeyType keytype;
	int payloadlen;
	class Utils *utils;
	char *header;
	int node_address_size;

	Index(char* indexName, KeyType *keytype, int payloadlen){
		utils = new Utils();
		fHandler = new FileHandler(indexName);
		this->keytype.numAttrs = keytype->numAttrs;
		for (int i=0; i < keytype->numAttrs;i++) {
			this->keytype.attrTypes[i] = keytype->attrTypes[i];
			this->keytype.attrLen[i] = keytype->attrLen[i];
		}
		this->payloadlen = payloadlen;
		header = (char *)malloc(BLOCK_SIZE);
		utils->copyBytes(&header[NODE_OFFSET_SIZE],utils->getBytesForInt(payloadlen),sizeof(int));
		utils->copyBytes(&header[NODE_OFFSET_SIZE+sizeof(payloadlen)],utils->getBytesForKeyType(this->keytype),sizeof(keytype));
	}

	//returns -1 if first value is smaller
	int compare(char *received_key, char *received_nodeKey) {
		char *key = received_key, *nodeKey = received_nodeKey;
		for (int i=0; i < keytype.numAttrs; i++){
			switch(keytype.attrTypes[i]) {
			case intType:
				if ( (int)(*key) > (int)(*nodeKey) )
					return 1;
				else if ( (int)(*key) < (int)(*nodeKey) )
					return -1;
				break;

			case stringType:
				int result = strncmp (key, nodeKey, keytype.attrLen[i]);
				if (result != 0 )
						return result;
				break;
			}
			key = key + keytype.attrLen[i];
			nodeKey = nodeKey + keytype.attrLen[i];
		}
		return 0;
	}


	Index(char* indexName){
		fHandler = new FileHandler(indexName);
		header = (char *)malloc(BLOCK_SIZE);
		fHandler->readBlock(0,header);
		utils = new Utils();
		utils->copyBytes(rootAddress,header,node_address_size);
		root = new TreeNode();
		loadNode(root,rootAddress);
		payloadlen= utils->getIntForBytes(&header[node_address_size]);
		keytype = utils->getKeyTypeForBytes(&header[node_address_size+sizeof(payloadlen)]);
	}
	int storeNode(TreeNode *node, int offset){
			if(offset == -1)
			{
				offset = fHandler->getSize();

			}
			utils->copyBytes(node->myaddr,utils->getBytesForInt(offset),NODE_OFFSET_SIZE);
			char *block = (char *)malloc(BLOCK_SIZE);
			int position = 0;
			utils->copyBytes(&block[position],utils->getBytesForInt(offset),NODE_OFFSET_SIZE);
			position += NODE_OFFSET_SIZE;
			block[position]=node->flag;
			position += 1;
			utils->copyBytes(&block[position],utils->getBytesForInt(node->numkeys),sizeof(node->numkeys));
			position += sizeof(node->numkeys);
			utils->copyBytes(&block[position],(node->data),sizeof(node->data));
			fHandler->writeBlock(offset,block);
			free(block);
			return 0;
		}

	int loadNode(TreeNode *here,char *offset){
		int position=0;
		char *block = (char *)malloc(BLOCK_SIZE);
		fHandler->readBlock(utils->getIntForBytes(offset),block);
		utils->copyBytes(here->myaddr,offset,NODE_OFFSET_SIZE);
		position+= NODE_OFFSET_SIZE;
		here->flag = block[position];
		position+=1;
		here->numkeys = utils->getIntForBytes(&(block[position]));
		position += sizeof(here->numkeys);
		utils->copyBytes(here->data,&(block[position]),sizeof(here->data));
		free(block);
		return 0;
	}

	int insert(char key[], char payload[]){
		if(root == 0)
		{
			addFirstElement(key,payload);
			return 0;
		}
		TreeNode * current = root;
		char *nodekey;
		nodekey = (char *)malloc(keylen(&keytype));
		char accessPath[MAX_TREE_HEIGHT][NODE_OFFSET_SIZE];
		int height = 0, i;
		while(current != 0)
		{

			utils->copyBytes((accessPath[height++]),(current->myaddr),NODE_OFFSET_SIZE);
			for (i = 0 ; i<current->numkeys ; i++ )
			{
				current->getKey(keytype,nodekey,i);
				int isLesser = compare(nodekey,key);
				if ( isLesser != -1)
				{
					break;
				}
			}

			if (current->flag == 'c')
				handleLeaf(key, payload, &current, i, accessPath,height);
			else
				handleNonLeaf(&current, i);
		}
		free(nodekey);
		return 0;
	}

	int addFirstElement(byte *key,byte *payload)
	{
		root = new TreeNode();
		root->numkeys = 0;
		root->flag = 'c';
		root->addData(keytype,key,payloadlen,payload,0);
		root->numkeys = 1;
		utils->copyBytes(header,utils->getBytesForInt(1),NODE_OFFSET_SIZE);
		utils->copyBytes(rootAddress,utils->getBytesForInt(1),NODE_OFFSET_SIZE);
		fHandler->writeBlock(0,header);
		storeNode(root,1);
        return 0;
	}
	int handleNonLeaf(TreeNode **rcvd_node, int position) {
		TreeNode *node=*rcvd_node;
		char *nextNodeAddress;
		nextNodeAddress = (char *)malloc(NODE_OFFSET_SIZE);
		node->getPayload(NODE_OFFSET_SIZE,nextNodeAddress,position);
//		********FREE PREVIOUS NODE HERE
		loadNode(*rcvd_node,nextNodeAddress);
		return 0;
	}

	int handleLeaf(byte key[], byte payload[], TreeNode **rcvd_node, int position, char accessPath[][NODE_OFFSET_SIZE],int height) {
		TreeNode *node=*rcvd_node;
		if(splitNecessary(node->numkeys+1,node->flag) != 1)
		{
			node->addData(keytype,key,payloadlen,payload,position);
			node->numkeys = node->numkeys + 1;
		}
		else
		{
			TreeNode *newLeaf = new TreeNode();
			int tempSpaceSize = DATA_SIZE+payloadlen+keylen(&keytype);
			char tempSpace[130];
			utils->copyBytes(tempSpace,node->data,(node->numkeys)*keylen(&keytype));
			utils->copyBytes(&tempSpace[tempSpaceSize-(node->numkeys)*payloadlen],&(node->data[DATA_SIZE-(node->numkeys)*payloadlen]),(node->numkeys)*payloadlen);
			for(int j = node->numkeys-1; j >= (position); j--) {
					utils->copyBytes(&(tempSpace[(j+1)*keylen(&keytype)]), &(tempSpace[j*keylen(&keytype)]),keylen(&keytype));
				}
			utils->copyBytes(&(tempSpace[(position)*keylen(&keytype)]),key, keylen(&keytype));

			for(int j = (tempSpaceSize-node->numkeys*payloadlen); j < (tempSpaceSize-position*payloadlen); j+=payloadlen) {
					utils->copyBytes(&(tempSpace[j-payloadlen]), &(tempSpace[j]),payloadlen);
			}
			utils->copyBytes(&(tempSpace[tempSpaceSize-(position+1)*payloadlen]),payload,payloadlen);
			node->numkeys = node->numkeys+1;
			int n_by_two = (node->numkeys)/2;
			for(int i = 0 ; i < n_by_two ; i++)
			{
				utils->copyBytes(&(node->data[(i)*keylen(&keytype)]),&(tempSpace[(i*keylen(&keytype))]),keylen(&keytype));
				utils->copyBytes(&(node->data[DATA_SIZE-((i+1)-n_by_two)*payloadlen]),&(tempSpace[tempSpaceSize-((i+1)*payloadlen)]),payloadlen);
			}
			for(int i = n_by_two; i< node->numkeys ; i++)
			{
				utils->copyBytes(&(newLeaf->data[(i-n_by_two)*keylen(&keytype)]),&(tempSpace[(i*keylen(&keytype))]),keylen(&keytype));
				utils->copyBytes(&(newLeaf->data[DATA_SIZE-((i+1)-n_by_two)*payloadlen]),&(tempSpace[tempSpaceSize-((i+1)*payloadlen)]),payloadlen);
			}
			newLeaf->flag = 'c';
			newLeaf->numkeys = node->numkeys - n_by_two;
			node->numkeys = n_by_two;
			TreeNode* parent = new TreeNode();
			for(int i = 0 ; i < height ; i++)
				if(accessPath[i] == (node->myaddr))
					loadNode(parent,accessPath[i]);
			char* nextKey =(char *) malloc(keylen(&keytype));
			newLeaf->getKey(keytype,nextKey,0);
			storeNode(newLeaf,-1);
			insertIntoParent(node->myaddr,nextKey,newLeaf->myaddr,parent->myaddr,height,accessPath);
		}
		*rcvd_node=0;
		return 0;
	}
	int insertIntoParent(byte left[NODE_OFFSET_SIZE],byte key[],byte right[NODE_OFFSET_SIZE],byte parentOffset[NODE_OFFSET_SIZE],int height,char accessPath[][NODE_OFFSET_SIZE]){
		if(strncmp(rootAddress,left,NODE_OFFSET_SIZE) == 0)
		{
			TreeNode *newRoot = new TreeNode();
			newRoot->numkeys = 1;
			newRoot->addData(keytype,key,NODE_OFFSET_SIZE,left,0);
			utils->copyBytes(&(newRoot->data[DATA_SIZE-NODE_OFFSET_SIZE*2]),right,NODE_OFFSET_SIZE);
			root = newRoot;
			storeNode(newRoot,-1);
			utils->copyBytes(rootAddress,root->myaddr,NODE_OFFSET_SIZE);
			byte *block = (byte *)malloc(BLOCK_SIZE);
			fHandler->readBlock(0,block);
			utils->copyBytes(block,rootAddress,NODE_OFFSET_SIZE);
			fHandler->writeBlock(0,block);
			free(block);
			return 0;
		}

		TreeNode *parent = new TreeNode();
		loadNode(parent,parentOffset);
		int i;
		char *nodekey;
		nodekey = (char *)malloc(keylen(&keytype));
		for (i = 0 ; i < parent->numkeys ; i++ )
		{
			parent->getKey(keytype,nodekey,i);
			int isLesser = compare(nodekey,key);
			if ( isLesser != -1)
			{
				break;
			}
		}

		parent->addData(keytype,key,NODE_OFFSET_SIZE,right,NODE_OFFSET_SIZE);
		parent->numkeys = parent->numkeys +1;
		if(splitNecessary(parent->numkeys,'n'))
		{
			TreeNode *newNonLeaf = new TreeNode();
			int n_by_two = (parent->numkeys)/2;
			for(int i = n_by_two+1; i< parent->numkeys ; i++)
			{
				parent->getKey(keytype,&(newNonLeaf->data[(i-n_by_two+1)*keylen(&keytype)]),i);
				parent->getPayload(payloadlen,&(newNonLeaf->data[DATA_SIZE-((i+1)-n_by_two+1)*payloadlen]),payloadlen);
			}
			newNonLeaf->flag = 'n';
			newNonLeaf->numkeys = n_by_two-1;
			parent->numkeys = n_by_two;
			TreeNode* grandParent = new TreeNode();
			for(int i = 0 ; i < height ; i++)
				if((accessPath[i])==(parent->myaddr))
					loadNode(grandParent,accessPath[i-1]);
			char* nextKey =(char *) malloc(keylen(&keytype));
			parent->getKey(keytype,nextKey,0);
			storeNode(newNonLeaf,-1);
			insertIntoParent(parent->myaddr,nextKey,newNonLeaf->myaddr,grandParent->myaddr,height,accessPath);
		}
		return 0;
	}
	int splitNecessary(int numkeys,char type){
		int allowedKeys;
		if(type == 'c')
			allowedKeys = (DATA_SIZE)/((keylen(&keytype)+payloadlen)+NODE_OFFSET_SIZE);
		else
			allowedKeys = (DATA_SIZE)/((keylen(&keytype)+NODE_OFFSET_SIZE)+NODE_OFFSET_SIZE);
		if(numkeys > allowedKeys)
			return 1;
		return 0;
	}

	int lookup(char key[], char payload[]){
		if(root == 0) {
			printf("BPlus Tree empty.");
			return 1;
		}
		TreeNode * current = root;
		char *nodekey;
		nodekey = (char *)malloc(keylen(&keytype));
		int i, isLesser;
		while(current != 0) {
			for (i = 0 ; i<current->numkeys ; i++ ) {
				current->getKey(keytype,nodekey,i);
				isLesser = compare(nodekey,key);
				if ( isLesser != -1) {
					break;
				}
			}

			if (current->flag == 'c') {
				if (isLesser != 0)	//key not found
					return 1;

				//key found, copy payload
				utils->copyBytes(payload,&(current->data[DATA_SIZE-(i+1)*payloadlen]),payloadlen);
				return 0;
			}
			else
				handleNonLeaf(&current, i);
		}
		return 1;
	}
};

int main(){
	KeyType keyType;
	keyType.numAttrs=1;
	keyType.attrTypes[0]=stringType;
	keyType.attrLen[0]=26;
	char *filename = "/home/sandeep/work/cs631/BPlusTree/index.ind";
	class Index *index = new Index(filename,&keyType,8);

	index->insert("2","2");
	index->insert("3","3");
	index->insert("1","1");
	index->insert("5","5");
	index->insert("33","33");
	index->insert("23","23");
	index->insert("7","7");
	return 0;
}
