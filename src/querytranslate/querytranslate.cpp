/* gMark
 * Copyright (C) 2015-2016 Guillaume Bagan <guillaume.bagan@liris.cnrs.fr>
 * Copyright (C) 2015-2016 Angela Bonifati <angela.bonifati@univ-lyon1.fr>
 * Copyright (C) 2015-2016 Radu Ciucanu <ciucanu@isima.fr>
 * Copyright (C) 2015-2016 George Fletcher <g.h.l.fletcher@tue.nl>
 * Copyright (C) 2015-2016 Aurélien Lemay <aurelien.lemay@univ-lille3.fr>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **/




/*  converting queries in gMark's XML syntax to queries in LogiQL, SQL:1999, SPARQL 1.1,
 *  and Cypher syntax
 */

#include "querytranslate.h"

void qtranslate(const string & inputfilename, const string & output_directory)
{
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(inputfilename.c_str()); 
    ofstream sqlout, sparqlout, cypherout, logicbloxout;
    int qcount = 0;

    if (! result)
    {
        cout << endl << "XML file [" << inputfilename << "] parsed with errors"<< endl;
        cout << "Error description: " << result.description() << endl;
        cout << "Error offset: " << result.offset << endl << endl;
        return;
    }

    for (pugi::xml_node query : doc.child("queries").children("query"))
    {
        cypherout.open(output_directory + "/query-" + to_string(qcount) + ".cypher");
        qtranslate_cypher(query, cypherout);
        cypherout.close();

        logicbloxout.open(output_directory + "/query-" + to_string(qcount) + ".succinct");
        qtranslate_succinct(query, logicbloxout);
        logicbloxout.close();

        qcount++;
    }
    cout << endl << endl << qcount << " queries processed" << endl << endl;
}


// translate one rule body into Succinct
//
void qtranslate_succinct_body(pugi::xml_node body, string outputVariables, ofstream & file)
{
  int c = 0;
  bool distinct = true;
  bool first = true;
  bool firstSymbol = true;
  bool star = false;
  string whereClause = "";
  string srcVar = "";
  string trgVar = "";
  pugi::xml_node currentNode, currentSymbol;

  if (body.child("conjunct").empty()) 
    return;

  for (pugi::xml_node conjunct : body.children("conjunct"))
  {
      if (! first)
        file << "\t";

      // process source and target of conjunct
      srcVar = conjunct.attribute("src").value();
      trgVar = conjunct.attribute("trg").value();
      if (srcVar.length() == 0 || trgVar.length() == 0)
      {
          cerr << "Bad input file: malformed conjunct";
          exit(EXIT_FAILURE);
      }
      if (srcVar[0] != '?')
      {
        // src is a constant
        if (whereClause.length() == 0) 
          whereClause = "id(gmarkvariable" + srcVar + ") = " + srcVar;
        else
          whereClause += " AND id(gmarkvariable" + srcVar + ") = " + srcVar;
      }
      if (trgVar[0] != '?')
      {
        // trg is a constant
        if (whereClause.length() == 0) 
          whereClause = "id(gmarkvariable" + trgVar + ") = " + trgVar;
        else
          whereClause += " AND id(gmarkvariable" + trgVar + ") = " + trgVar;
      }

      // process the conjunct's predicate
      star = false;
      if (conjunct.child("star"))
      {
        currentNode = conjunct.child("star").child("disj").child("concat");
        star = true;
      }
      else if (conjunct.child("disj"))
      {
        currentNode = conjunct.child("disj").child("concat");
      }
      else
      {
        cerr << "Bad input file: malformed conjunct";
        exit(EXIT_FAILURE);
      }

      if (!currentNode)
      {
          cerr << "Bad input file: malformed star";
          exit(EXIT_FAILURE);
      }

      if (star)
      {
        // (s)-[r*]->(t) 
        // WHERE ALL (x in type(r) WHERE x=type1 OR ... OR x=typeN)
        // file << "(" << ((srcVar[0] == '?') ? srcVar.substr(1) : "gmarkvariable" + srcVar) << ")-[";
        file << "((";
        firstSymbol = true;
        std::set<std::string> done;
        string prv_symb = "";
        do
        {
          std::string symb = (string)currentNode.child("symbol").text().get();
          if (!firstSymbol) {
            if (done.count(symb) == 0) {
              file << "+(" << (string)currentNode.child("symbol").text().get() << ")";
              done.insert(symb);
            }
          } else {
            file << "(" << symb << ")";
            done.insert(symb);
          }

          firstSymbol = false;
          prv_symb = (string) currentNode.child("symbol").text().get();
          currentNode = currentNode.next_sibling("concat"); 
        } while (currentNode);

        file << "))*";
        c++;
      }
      else
      {
        currentSymbol = currentNode.child("symbol");
        if (!currentSymbol)
        {
          cerr << "Bad input file: malformed conjunct structure";
          exit(EXIT_FAILURE);
        }

        firstSymbol = true;
        file << "(((";
        do 
        {
          string valueedge (currentSymbol.attribute("inverse").value());
          if (valueedge.compare("true") == 0) 
          {
            // ()<-[p]-() 
            if (! firstSymbol) 
            {
              file << "." << currentSymbol.text().get() << "-";
            }
            else
            {
              file << currentSymbol.text().get() << "-";
              firstSymbol = false;
            }
          }
          else 
          {
            // ()-[p]->() 
            if (! firstSymbol) 
            { 
              file << "." << currentSymbol.text().get();
            }
            else
            {
              file << currentSymbol.text().get();
              firstSymbol = false;
            }
          }
          currentSymbol = currentSymbol.next_sibling("symbol"); 
        } while (currentSymbol);
        file << ")))";
      }
      first = false;
  }
}


// convert queries in input XML doc into restricted syntax where all non-recursive
// conjuncts have only one disjunct (of arbitrary path length)
void qtranslate_succinct_doc(pugi::xml_document & doc)
{
  pugi::xml_node copy, querycopy, newbody; 
  pugi::xml_document tempDoc;
  vector < pugi::xml_node > conjunctList;
  vector < pugi::xml_node > removeList;

  tempDoc.append_child("queries");
  tempDoc.append_child("scratch");

  for (pugi::xml_node query: doc.child("queries").children("query"))
  { 
    querycopy = tempDoc.child("queries").append_copy(query);
    while (querycopy.child("bodies").remove_child("body"));

    for (pugi::xml_node body : query.child("bodies").children("body"))
    {
      conjunctList.clear();
      newbody = tempDoc.child("scratch").append_child("body");

      for (pugi::xml_node conjunct : body.children("conjunct"))
      {
        if (conjunct.child("disj")) 
        { 
          copy = tempDoc.child("scratch").append_copy(conjunct);
          conjunctList.push_back(copy);
        }
        else
          newbody.append_copy(conjunct);
      }

      qtranslate_succinct_doc_reconstructBody(querycopy, newbody, conjunctList);
    }
  }

  doc.remove_child("queries");
  doc.append_copy(tempDoc.child("queries"));
}

void qtranslate_succinct_doc_reconstructBody(pugi::xml_node & query, 
                                           pugi::xml_node & body, 
                                           vector < pugi::xml_node > conjunctList)
{ 
  pugi::xml_node current, currentcopy, disjunct, bodycopy; 
  pugi::xml_document tempDoc;

  if (conjunctList.empty()) 
  {
    query.child("bodies").append_copy(body);
  }
  else
  {
    current = conjunctList.back();
    conjunctList.pop_back();
    currentcopy = tempDoc.append_copy(current);
    while (currentcopy.child("disj").remove_child("concat"));
    for (pugi::xml_node concat : current.child("disj").children("concat"))
    {
      disjunct = currentcopy.child("disj").append_copy(concat);
      bodycopy = body.append_copy(currentcopy);
      qtranslate_succinct_doc_reconstructBody(query, body, conjunctList);
      body.remove_child(bodycopy);
      currentcopy.child("disj").remove_child(disjunct);
    }
  }
}

// translate a query into Cypher
//
// NOTE: Cypher only supports kleene star on a disjunction of single edge labels, i.e., of
// the form (a1 + ... + an)*, where each ai is a single symbol.  Furthermore, inverse
// labels can not appear under Kleene star.  (see email exchange of June 2015 with Tobias
// Lindaaker of Neo Technology)
//
void qtranslate_succinct(pugi::xml_node query, ofstream & file) 
{
  bool first = true;
  string outputVariables = "";
  string oneVar;

  // collect projection list
  for (pugi::xml_node var_node : query.child("head").children("var"))
  {
    if (first)
    {
      oneVar = var_node.text().get();
      outputVariables +=  oneVar.substr(1);
      first = false;
    }
    else
    {
      oneVar = var_node.text().get();
      outputVariables +=  ", " + oneVar.substr(1);
    }
  }

  first = true;

  // a union over bodies
  // for each body, construct a MATCH-WHERE-RETURN statement
  for (pugi::xml_node body : query.child("bodies").children("body"))
  {
      // if (! first)
      //   file << " UNION ";
      // else
      //   first = false;

      qtranslate_succinct_body(body, outputVariables, file);
  }
  file << endl;
}

// translate one rule body into Cypher
// NOTE: see comments for qtranslate_cypher()
//
void qtranslate_cypher_body(pugi::xml_node body, string outputVariables, ofstream & file)
{
  int c = 0;
  bool distinct = true;
  bool first = true;
  bool firstSymbol = true;
  bool star = false;
  string whereClause = "";
  string srcVar = "";
  string trgVar = "";
  pugi::xml_node currentNode, currentSymbol;

  if (body.child("conjunct").empty()) 
    return;

  file << "MATCH ";

  for (pugi::xml_node conjunct : body.children("conjunct"))
  {
      if (! first)
        file << ", ";

      // process source and target of conjunct
      srcVar = conjunct.attribute("src").value();
      trgVar = conjunct.attribute("trg").value();
      if (srcVar.length() == 0 || trgVar.length() == 0)
      {
          cerr << "Bad input file: malformed conjunct";
          exit(EXIT_FAILURE);
      }
      if (srcVar[0] != '?')
      {
        // src is a constant
        if (whereClause.length() == 0) 
          whereClause = "id(gmarkvariable" + srcVar + ") = " + srcVar;
        else
          whereClause += " AND id(gmarkvariable" + srcVar + ") = " + srcVar;
      }
      if (trgVar[0] != '?')
      {
        // trg is a constant
        if (whereClause.length() == 0) 
          whereClause = "id(gmarkvariable" + trgVar + ") = " + trgVar;
        else
          whereClause += " AND id(gmarkvariable" + trgVar + ") = " + trgVar;
      }

      // process the conjunct's predicate
      star = false;
      if (conjunct.child("star"))
      {
        currentNode = conjunct.child("star").child("disj").child("concat");
        star = true;
      }
      else if (conjunct.child("disj"))
      {
        currentNode = conjunct.child("disj").child("concat");
      }
      else
      {
        cerr << "Bad input file: malformed conjunct";
        exit(EXIT_FAILURE);
      }

      if (!currentNode)
      {
          cerr << "Bad input file: malformed star";
          exit(EXIT_FAILURE);
      }

      if (star)
      {
        // (s)-[r*]->(t) 
        // WHERE ALL (x in type(r) WHERE x=type1 OR ... OR x=typeN)
        file << "(" << ((srcVar[0] == '?') ? srcVar.substr(1) : "gmarkvariable" + srcVar) << ")-[";

        firstSymbol = true;
        do
        {
          if (! firstSymbol) 
            file << "|p" << (string)currentNode.child("symbol").text().get();
          else
            file << ":p" << (string)currentNode.child("symbol").text().get();

          firstSymbol = false;
          currentNode = currentNode.next_sibling("concat"); 
        } while (currentNode);

        file << "*]->(" << ((trgVar[0] == '?') ? trgVar.substr(1) : "gmarkvariable" + trgVar) << ")";

        /*
        if (whereClause.length() == 0) 
          whereClause = "ALL (x IN RELATIONSHIPS(gmarkpath" + to_string(c) + ") WHERE";
        else
          whereClause += " AND ALL (x IN RELATIONSHIPS(gmarkpath" + to_string(c) + ") WHERE";
        firstSymbol = true;
        do
        {
          if (! firstSymbol) 
            whereClause += " OR TYPE(x)=p" + (string)currentNode.child("symbol").text().get();
          else
            whereClause += " TYPE(x)=p" + (string)currentNode.child("symbol").text().get();
          firstSymbol = false;
          currentNode = currentNode.next_sibling("concat"); 
        } while (currentNode);
        whereClause += ")";
        */

        c++;
      }
      else
      {
        currentSymbol = currentNode.child("symbol");
        if (!currentSymbol)
        {
          cerr << "Bad input file: malformed conjunct structure";
          exit(EXIT_FAILURE);
        }

        firstSymbol = true;
        file << "(" << ((srcVar[0] == '?') ? srcVar.substr(1) : "gmarkvariable" + srcVar) << ")";
        do 
        {
          string valueedge (currentSymbol.attribute("inverse").value());
          if (valueedge.compare("true") == 0) 
          {
            // ()<-[p]-() 
            if (! firstSymbol) 
            {
              file << "()<-[:p" << currentSymbol.text().get() << "]-";
            }
            else
            {
              file << "<-[:p" << currentSymbol.text().get() << "]-";
              firstSymbol = false;
            }
          }
          else 
          {
            // ()-[p]->() 
            if (! firstSymbol) 
            { 
              file << "()-[:p" << currentSymbol.text().get() << "]->";
            }
            else
            {
              file << "-[:p" << currentSymbol.text().get() << "]->";
              firstSymbol = false;
            }
          }
          currentSymbol = currentSymbol.next_sibling("symbol"); 
        } while (currentSymbol);
        file << "(" << ((trgVar[0] == '?') ? trgVar.substr(1) : "gmarkvariable" + trgVar) << ")";
      }
      first = false;
  }

  if(whereClause.length() > 0) 
    file << " WHERE " << whereClause;

  file << " RETURN x0";
  // if(outputVariables.length() == 0) 
  // {
  //   // a boolean query
  //   file << " RETURN \"true\" LIMIT 1";
  // }
  // else
  // {
  //   if (distinct) 
  //   {
  //     file << " RETURN DISTINCT " << outputVariables;
  //     //hack for selectivity experiments
  //     //file << " RETURN count(*)";

  //   }
  //   else 
  //     file << " RETURN " << outputVariables;

  // }
}


// convert queries in input XML doc into restricted syntax where all non-recursive
// conjuncts have only one disjunct (of arbitrary path length)
void qtranslate_cypher_doc(pugi::xml_document & doc)
{
  pugi::xml_node copy, querycopy, newbody; 
  pugi::xml_document tempDoc;
  vector < pugi::xml_node > conjunctList;
  vector < pugi::xml_node > removeList;

  tempDoc.append_child("queries");
  tempDoc.append_child("scratch");

  for (pugi::xml_node query: doc.child("queries").children("query"))
  { 
    querycopy = tempDoc.child("queries").append_copy(query);
    while (querycopy.child("bodies").remove_child("body"));

    for (pugi::xml_node body : query.child("bodies").children("body"))
    {
      conjunctList.clear();
      newbody = tempDoc.child("scratch").append_child("body");

      for (pugi::xml_node conjunct : body.children("conjunct"))
      {
        if (conjunct.child("disj")) 
        { 
          copy = tempDoc.child("scratch").append_copy(conjunct);
          conjunctList.push_back(copy);
        }
        else
          newbody.append_copy(conjunct);
      }

      qtranslate_cypher_doc_reconstructBody(querycopy, newbody, conjunctList);
    }
  }

  doc.remove_child("queries");
  doc.append_copy(tempDoc.child("queries"));
}

void qtranslate_cypher_doc_reconstructBody(pugi::xml_node & query, 
                                           pugi::xml_node & body, 
                                           vector < pugi::xml_node > conjunctList)
{ 
  pugi::xml_node current, currentcopy, disjunct, bodycopy; 
  pugi::xml_document tempDoc;

  if (conjunctList.empty()) 
  {
    query.child("bodies").append_copy(body);
  }
  else
  {
    current = conjunctList.back();
    conjunctList.pop_back();
    currentcopy = tempDoc.append_copy(current);
    while (currentcopy.child("disj").remove_child("concat"));
    for (pugi::xml_node concat : current.child("disj").children("concat"))
    {
      disjunct = currentcopy.child("disj").append_copy(concat);
      bodycopy = body.append_copy(currentcopy);
      qtranslate_cypher_doc_reconstructBody(query, body, conjunctList);
      body.remove_child(bodycopy);
      currentcopy.child("disj").remove_child(disjunct);
    }
  }
}

// translate a query into Cypher
//
// NOTE: Cypher only supports kleene star on a disjunction of single edge labels, i.e., of
// the form (a1 + ... + an)*, where each ai is a single symbol.  Furthermore, inverse
// labels can not appear under Kleene star.  (see email exchange of June 2015 with Tobias
// Lindaaker of Neo Technology)
//
void qtranslate_cypher(pugi::xml_node query, ofstream & file) 
{
  bool first = true;
  string outputVariables = "";
  string oneVar;

  // collect projection list
  for (pugi::xml_node var_node : query.child("head").children("var"))
  {
    if (first)
    {
      oneVar = var_node.text().get();
      outputVariables +=  oneVar.substr(1);
      first = false;
    }
    else
    {
      oneVar = var_node.text().get();
      outputVariables +=  ", " + oneVar.substr(1);
    }
  }

  // first = true;

  // a union over bodies
  // for each body, construct a MATCH-WHERE-RETURN statement
  for (pugi::xml_node body : query.child("bodies").children("body"))
  {
      // if (! first)
      //   file << " UNION ";
      // else
      //   first = false;

      qtranslate_cypher_body(body, outputVariables, file);
  }
  file << ";" << endl;
}