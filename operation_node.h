/*******************************************************************************
 * Filename: operation_mode.h
 * Purpose:  Defines the various nodes for the query tree
 * Author:   Yang Guang, Neeraj Rao
 ******************************************************************************/
#ifndef OPERNODE_
#define OPERNODE_
#include <string>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include "ParseTree.h"
#include "Comparison.h"
#include "a3utils.h"
#include "Defs.h"

using namespace std;
extern unordered_map<string, relation*> DBinfo;

/*******************************************************************************
 * Helper function to print output schema of each node
 ******************************************************************************/
void PrintOutputSchema(Schema* s){
  cout << "Output Schema: " << endl;
  for(int i = 0;i<s->GetNumAtts();i++){
    Attribute *att = s->GetAtts();
    cout << "    " << att[i].name << ": ";
    // cout << "    Att" << (i+1) << ": ";
    switch(att[i].myType){
      case 0:
        cout << "Int" << endl;
        break;
      case 1:
        cout << "Double" << endl;
        break;
      case 2:
        cout << "String" << endl;
        break;
    }
  }
}

/*******************************************************************************
 * Generic base class for the different query tree nodes
 ******************************************************************************/
class GenericQTreeNode {
  protected:
    Schema *rschema;

  public:
    GenericQTreeNode* left;
    GenericQTreeNode* right;
    // the outpipe that output according to the output schema.
    Pipe* outpipe;
    int pipeID;

    GenericQTreeNode(){
      left = NULL;
      right = NULL;

      // create the output pipe so that we can call Run on the nodes
      // without worrying about the order of traversal of the tree
      // (we actually use pre-order traversal, but this way, any
      // traversal method would work)
      outpipe = new Pipe (pipesz);
    };

    virtual Schema* schema(){};
    virtual ~GenericQTreeNode(){};
    virtual void Print(){};
    virtual void Run(){};
    virtual void WaitUntilDone(){};
};

/*******************************************************************************
 * Tree Node for Select Pipe operation
 * There may be more than one of these in the query tree.
 ******************************************************************************/
class Selection_PNode : virtual public GenericQTreeNode {
  private:
    string RelName;
    Record literal;
    CNF cnf_pred;
    SelectPipe SP;
  public:
    Selection_PNode(struct AndList &dummy, string &RelName, unordered_map<string, GenericQTreeNode*> &relNameToTreeMap, int pipeIDcounter,unordered_map<string, string> &nodeAlias){
      GenericQTreeNode();

      if(nodeAlias.count(RelName))
        RelName=nodeAlias[RelName];

      this->RelName = RelName;

      GenericQTreeNode* lSubT = NULL;
      // if tree structure already exist for att name, retrieve the subtree.
      if(relNameToTreeMap.count(RelName))
        lSubT = relNameToTreeMap[RelName];

      // connect the tree structure. update the name-treeNode pointer hash.
      left = lSubT;
      relNameToTreeMap[RelName] = this;

      // inherit the schema from its left child
      rschema = left->schema();

      pipeID = pipeIDcounter;

      // create the CNF from schema.
      cnf_pred.GrowFromParseTree (&dummy, left->schema(), literal);
    };

    Schema* schema () {
      return rschema;
    }

    ~Selection_PNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "   SELECT PIPE OPERATION" << endl;
      cout << "   ---------------------" << endl;
      cout << "Input pipe ID: " << left->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(left->schema());
      cout << "Select CNF: " << endl << "    ";
      cnf_pred.Print();
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "selectpipe started" << endl; // debug
      SP.Use_n_Pages (buffsz);
      SP.Run (*(left->outpipe), *outpipe, cnf_pred, literal); // Select Pipe takes its input from its left child's
                                                              // outPipe. Its right child is NULL.
    };

    void WaitUntilDone(){
      // cout << "selectpipe ended" << endl; // debug
      SP.WaitUntilDone ();
    }
};

/*******************************************************************************
 * Tree Node for Select File operation
 * There may be more than one of these in the query tree.
 ******************************************************************************/
class Selection_FNode : virtual public GenericQTreeNode {
  private:
    string RelName;
    relation* rel;
    Record literal;
    CNF cnf_pred;
    SelectFile SF;
    DBFile dbfile;
  public:
    Selection_FNode(struct AndList &dummy, string &RelName, unordered_map<string, GenericQTreeNode*> &relNameToTreeMap, int pipeIDcounter){
      GenericQTreeNode();
      this->RelName = RelName;

      // use a stringstream to store output from the recursive call of PrintOrList
      rel = DBinfo[RelName];

      // init the schema
      rschema = rel->schema();

      pipeID = pipeIDcounter;

      // create the CNF from schema.
      cnf_pred.GrowFromParseTree (&dummy, rel->schema(), literal);

      // connect the tree structure. update the name-treeNode pointer hash.
      relNameToTreeMap[RelName] = this;
    };

    Schema* schema () {
      return rschema;
    }

    ~Selection_FNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "   SELECT FILE OPERATION" << endl;
      cout << "   ---------------------" << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rel->schema());
      cout << "CNF: " << endl << "    ";
      cnf_pred.Print();
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "selectfile started" << endl; // debug
      dbfile.Open (rel->path());
      //dbfile.MoveFirst();
      SF.Use_n_Pages (buffsz);
      SF.Run (dbfile, *outpipe, cnf_pred, literal); // Select File takes its input from the disk.
    };

    void WaitUntilDone(){
      // cout << "selectfile ended" << endl; // debug
      SF.WaitUntilDone ();
      dbfile.Close();
    }
};

/*******************************************************************************
 * Tree Node for Project operation
 * Only ONE instance of this node will be in the Query Tree at any given time.
 * Group By, Sum and Project are mutually exlusive!!!
 ******************************************************************************/
class ProjectNode : virtual public GenericQTreeNode {
  private:
    NameList PAtts; // will be used for printing in Print()
    Project P;

    // variables that will be passed to the Project object in
    // ProjectNode.Run()
    int* keepMe;
    int numAttsIn, numAttsOut;

  public:
    ProjectNode(NameList *atts, GenericQTreeNode* &root, int& pipeIDcounter){
      GenericQTreeNode();
      left = root;
      root = this;

      PAtts = *atts; // will be used for printing in Print()
      NameList tempNameList = *atts; // create a temporary copy of atts because it will be destroyed
                                     // when we walk it to determine atts below

      pipeID = pipeIDcounter;
      pipeIDcounter++; // increment for next guy

      // init the schema
      Schema* inschema = left->schema();

      numAttsIn = inschema->GetNumAtts();

      // init variables that will be passed to the Project object in
      // ProjectNode.Run()
      vector <int> temp;
      vector <Type> tempType; // will be used below to prune inschema
      vector <char*> tempName; // will be used below to prune inschema
      NameList* tempL = &tempNameList;
      do{
        // if the input attribute has the relation name prepended along with a dot,
        // remove it
        char* dotStripped = strpbrk(tempL->name, ".")+1;
        if(dotStripped!=NULL){ // there was a dot and we removed the prefix
          tempL->name = dotStripped;
          temp.push_back(inschema->Find(dotStripped));
          tempType.push_back(inschema->FindType(dotStripped));
          tempName.push_back(dotStripped);
        }
        else{ // there was no dot; use the name as-is
          temp.push_back(inschema->Find(tempL->name));
          tempType.push_back(inschema->FindType(tempL->name));
          tempName.push_back(tempL->name);
        }
        tempL = tempL->next;
      }while(tempL);
      numAttsOut = temp.size();
      keepMe = new int[numAttsOut]();
      Attribute* tempAttArray = new Attribute[numAttsOut](); // will be used below to prune inschema
      for(int i=0;i<numAttsOut;i++){
        keepMe[i] = temp.at(i);
        tempAttArray[i].name = tempName.at(i);
        tempAttArray[i].myType = tempType.at(i);
      }

      // Prune the input schema to contain only the attributes that we are projecting upon
      // Save it as ProjectNode's rschema so that it can be used to parse the output in clear_pipe
      rschema = new Schema("projectOutputSchema",numAttsOut,tempAttArray);
    };

    Schema* schema () {
      return rschema;
    }

    ~ProjectNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "   PROJECTION OPERATION" << endl;
      cout << "   --------------------" << endl;
      cout << "Input pipe ID: " << left->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rschema);
      cout << "Project Attributes:" << endl;
      PrintNameList(&PAtts);
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "project started" << endl; // debug
      P.Use_n_Pages (buffsz);
      P.Run (*(left->outpipe), *outpipe, keepMe, numAttsIn, numAttsOut); // Project takes its input from its left child's
                                                                         // outPipe. Its right child is NULL.
    };

    void WaitUntilDone(){
      // cout << "project ended" << endl; // debug
      P.WaitUntilDone ();
    }
};

/*******************************************************************************
 * Tree Node for DuplicateRemoval operation
 * Only ONE instance of this node will be in the Query Tree at any given time.
 ******************************************************************************/
class DupRemNode : virtual public GenericQTreeNode {
  private:
    DuplicateRemoval D;

  public:
    DupRemNode(int distinctAtts, int distinctFunc, GenericQTreeNode* &root, int& pipeIDcounter){
      // There was no distinct clause in the input CNF; no need to add a DupRemNode to the query tree
      if(!(distinctAtts||distinctFunc))
        return;

      GenericQTreeNode();
      left = root;
      root = this;

      // inherit the schema from its left child
      rschema = left->schema();

      pipeID = pipeIDcounter;
      pipeIDcounter++; // increment for next guy
    };

    Schema* schema () {
      return rschema;
    }

    ~DupRemNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "DUPLICATE REMOVAL OPERATION" << endl;
      cout << "---------------------------" << endl;
      cout << "Input pipe ID: " << left->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rschema);
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "dupremoval started" << endl; // debug
      D.Use_n_Pages (buffsz);
      D.Run (*(left->outpipe), *outpipe, *rschema); // DuplicateRemoval takes its input from its left child's
                                                    // outPipe. Its right child is NULL.
    };

    void WaitUntilDone(){
      // cout << "dupremoval ended" << endl; // debug
      D.WaitUntilDone ();
    }

};

/*******************************************************************************
 * Tree Node for Sum operation
 * Only ONE instance of this node will be in the Query Tree at any given time.
 * Group By, Sum and Project are mutually exlusive!!!
 ******************************************************************************/
class SumNode : virtual public GenericQTreeNode {
  private:
    Function Func;
    Sum S;
    FuncOperator *funcOperator; // used for printing
    Schema* inSchema;
    Type returnType;

  public:
    SumNode(FuncOperator *funcOperator, GenericQTreeNode* &root, int& pipeIDcounter){
      GenericQTreeNode();
      left = root;
      root = this;

      // inherit the schema from its left child
      inSchema = left->schema();

      this->funcOperator=funcOperator;

      // initialize the Func object that will be passed to the Sum object in Run()
      Func.GrowFromParseTree (funcOperator, *inSchema); // constructs CNF predicate

      // Use double for int as well
      Attribute DA = {"Sum", Double};
      rschema = new Schema("outSchema",1,&DA);

      pipeID = pipeIDcounter;
      pipeIDcounter++; // increment for next guy
    };

    Schema* schema () {
      return rschema;
    }

    ~SumNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "       SUM OPERATION" << endl;
      cout << "       -------------" << endl;
      cout << "Input pipe ID: " << left->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rschema);
      cout << "Corresponding Function: " << endl;
      Func.Print(funcOperator,*rschema);
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "sum started" << endl; // debug
      S.Use_n_Pages (buffsz);
      S.Run (*(left->outpipe), *outpipe, Func); // Sum takes its input from its left child's
                                                    // outPipe. Its right child is NULL.
    };

    void WaitUntilDone(){
      // cout << "sum ended" << endl; // debug
      S.WaitUntilDone ();
    }

};

/*******************************************************************************
 * Tree Node for Group By operation
 * Only ONE instance of this node will be in the Query Tree at any given time.
 * Group By, Sum and Project are mutually exlusive!!!
 ******************************************************************************/
class Group_byNode : virtual public GenericQTreeNode {
  private:
    Function Func;
    NameList GAtts; // will be used for printing in Print()
    GroupBy G;
    myAtt* orderMakerAtts; // used to create the OrderMaker passed to GroupBy in Run
    OrderMaker grp_order; // used to create the OrderMaker passed to GroupBy in Run
    FuncOperator *funcOperator; // used for printing

  public:
    Group_byNode(NameList *gAtts, FuncOperator *funcOperator, GenericQTreeNode* &root, int& pipeIDcounter){
      GenericQTreeNode();
      left = root;
      root = this;

      GAtts = *gAtts; // will be used for printing in Print()
      NameList tempNameList = *gAtts; // create a temporary copy of gAtts because it will be destroyed
                                      // when we walk it to determine numGroupingAtts below

      // inherit the schema from its left child
      rschema = left->schema();

      // initialize the Func object that will be passed to the GroupBy object in Run()
      Func.GrowFromParseTree (funcOperator, *rschema); // constructs CNF predicate

      // We must craft an output schema that will have
      // 1. first attribute -- a Double (we use a Double even for ints)
      // 2. remaining attributes -- all the grouping attributes
      int numGroupingAtts = 0;
      do{
        numGroupingAtts++;
        gAtts = gAtts->next;
      }while(gAtts);

      orderMakerAtts = new myAtt[numGroupingAtts](); // used to create the OrderMaker
                                                     // passed to GroupBy in Run()

      numGroupingAtts++; // add 1 attribute for the sum attribute
      Attribute* outAtts = new Attribute[numGroupingAtts]; // used to create GroupBy's output schema
                                                           // that will be used to print in clear_pipe

      outAtts[0] = {"Sum", Double};

      // add the remaining attributes (the grouping attributes)
      int i=0;
      NameList* temp = &tempNameList;
      do{
        orderMakerAtts[i].attNo =  rschema->Find(temp->name);
        orderMakerAtts[i].attType = rschema->FindType(temp->name);
        i++;
        outAtts[i] = {temp->name,rschema->FindType(temp->name)};
        temp = temp->next;
      }while(temp);

      rschema = new Schema("out_sch", numGroupingAtts, outAtts);

      this->funcOperator=funcOperator;

      grp_order.initOrderMaker(numGroupingAtts-1,orderMakerAtts);

      pipeID = pipeIDcounter;
      pipeIDcounter++; // increment for next guy
    };

    Schema* schema () {
      return rschema;
    }

    ~Group_byNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "     GROUP BY OPERATION" << endl;
      cout << "     ------------------" << endl;
      cout << "Input pipe ID: " << left->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rschema);
      cout << "Grouping Attributes:" << endl;
      PrintNameList(&GAtts);
      cout << "Aggregate Function:" << endl;
      Func.Print(funcOperator,*rschema);
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "groupby started" << endl; // debug
      G.Use_n_Pages (buffsz);
      G.Run (*(left->outpipe), *outpipe, grp_order, Func); // GroupBy takes its input from its left child's
                                                           // outPipe. Its right child is NULL.
    };

    void WaitUntilDone(){
      // cout << "groupby ended" << endl; // debug
      G.WaitUntilDone ();
    }

};

/*******************************************************************************
 * Tree Node for Join operation
 * There may be more than one of these in the query tree.
 ******************************************************************************/
class JoinNode : virtual public GenericQTreeNode {
  private:
    string RelName0, RelName1;
    Record literal;
    CNF cnf_pred;
    Join J;

  public:
    JoinNode(struct AndList &dummy, string &RelName0, string &RelName1, unordered_map<string,GenericQTreeNode*> &relNameToTreeMap, int& pipeIDcounter,unordered_map<string, string> &nodeAlias){
      GenericQTreeNode();

      if(nodeAlias.count(RelName0))
        RelName0=nodeAlias[RelName0];
      if(nodeAlias.count(RelName1))
        RelName1=nodeAlias[RelName1];
      this->RelName0 = RelName0;
      this->RelName1 = RelName1;
      GenericQTreeNode* lSubT=NULL,*rSubT=NULL;

      // LEFT CHILD
      if(!relNameToTreeMap.count(RelName0)){
        // cout << RelName0 << " not in map; creating selectfile" << endl; // debug
        // this relation has not been encountered before this Join. Create a SelectFile Node
        // with an empty CNF (equivalent to a "Select *") to pipe in its input. 
        GenericQTreeNode* NewQNode;
        // Create an empty AndList so that the SelectFile node creates an empty CNF
        AndList tempAndList;
        tempAndList.left = NULL;
        tempAndList.rightAnd = NULL;
        NewQNode = new Selection_FNode(tempAndList, RelName0, relNameToTreeMap, pipeIDcounter);
        pipeIDcounter++;
        relNameToTreeMap[RelName0] = NewQNode;
      }
      // retrieve the subtree (if we went into the if block above, this subtree consists of only
      // the one SelectFile node).
      lSubT=relNameToTreeMap[RelName0];

      // RIGHT CHILD
      if(!relNameToTreeMap.count(RelName1)){
        // cout << RelName1 << " not in map; creating selectfile" << endl; // debug
        // this relation has not been encountered before this Join. Create a SelectFile Node
        // with an empty CNF (equivalent to a "Select *") to pipe in its input. 
        GenericQTreeNode* NewQNode;
        // Create an empty AndList so that the SelectFile node creates an empty CNF
        AndList tempAndList;
        tempAndList.left = NULL;
        tempAndList.rightAnd = NULL;
        NewQNode = new Selection_FNode(tempAndList, RelName1, relNameToTreeMap, pipeIDcounter);
        pipeIDcounter++;
        relNameToTreeMap[RelName1] = NewQNode;
      }
      // retrieve the subtree (if we went into the if block above, this subtree consists of only
      // the one SelectFile node).
      rSubT=relNameToTreeMap[RelName1];

      // connect this node to its subtrees
      left=lSubT;
      right=rSubT;

      // this node now takes its place in the hash as the root of the tree associated with the two
      // relations being joined
      relNameToTreeMap[RelName0]=this;
      relNameToTreeMap[RelName1]=this;

      // the right attribute will be joined (eliminated). delete the right attribute's subtree if it exists.
      if(relNameToTreeMap.count(RelName1))
        relNameToTreeMap.erase(RelName1);

      //added the removed RelName to nodeAlias hash, so that Select File and future joins can retrieves the latest merged schema.
      nodeAlias[RelName1]=RelName0;
      // replace all the names mapping to RelName1 in the hash to RelName0.
      for(auto it = nodeAlias.begin();it!=nodeAlias.end();it++){
        if((it->second).compare(RelName1)==0)it->second = RelName0;
      }

      // create the schema that will be used for parents of this node
      rschema=left->schema();
      rschema=rschema->mergeSchema(right->schema());

      pipeID=pipeIDcounter;

      // create the CNF from schema.
      cnf_pred.GrowFromParseTree (&dummy, left->schema(), right->schema(), literal);
    };

    Schema* schema () {
      return rschema;
    }

    ~JoinNode(){
    };

    void Print(){
      cout << endl;
      cout << "***************************" << endl;
      cout << "      JOIN OPERATION" << endl;
      cout << "      --------------" << endl;
      cout << "Input pipe 1 ID: " << left->pipeID << endl;
      cout << "Input pipe 2 ID: " << right->pipeID << endl;
      cout << "Output pipe ID: " << pipeID << endl;
      PrintOutputSchema(rschema);
      cout << "Join CNF: " << endl << "    ";
      cnf_pred.Print();
      cout << "***************************" << endl;
    };

    void Run(){
      // cout << "join started" << endl; // debug
      J.Use_n_Pages (buffsz);
      J.Run(*(left->outpipe),*(right->outpipe),*outpipe,cnf_pred,literal); // Join takes its input from its left and
                                                                           // right children's outpipes
    };

    void WaitUntilDone(){
      // cout << "join ended" << endl; // debug
      J.WaitUntilDone ();
    }

};

#endif
/*******************************************************************************
 * EOF
 ******************************************************************************/
