\LoadClass{article}

\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{ledger}


% PACKAGES
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

\RequirePackage[superscript,biblabel]{cite}

\RequirePackage{subfig}

\RequirePackage{numprint}
\npthousandsep{,}\npthousandthpartsep{}\npdecimalsign{.}
\newcommand{\num}[1]{\numprint{#1}}
\newcommand{\q}[2]{\num{#1}~{#2}}
\newcommand{\percent}[1]{\num{#1}\%}

\usepackage[utf8]{inputenc}
\usepackage[main=english,russian]{babel}


\RequirePackage{mathptmx} % Use the Adobe Times Roman as the default text font together with math symbols from the Symbol, Chancery and Computer Modern fonts
\renewcommand{\@xpt}{11.5}
\renewcommand{\@xiipt}{13.5}
\renewcommand\normalsize{%
   \@setfontsize\normalsize\@xpt\@xiipt
   \abovedisplayskip 10\p@ \@plus2\p@ \@minus5\p@
   \abovedisplayshortskip \z@ \@plus3\p@
   \belowdisplayshortskip 6\p@ \@plus3\p@ \@minus3\p@
   \belowdisplayskip \abovedisplayskip
   \let\@listi\@listI} % This 'sets' the font size to 11pt.  This is modified on from part of the source of size10.clo.


\RequirePackage{microtype} % Helps with overfull/underfull boxes

\setlength{\headheight}{34pt} % This is the minimum that fancyhdr asks for
\newlength{\topmarginheight} % Margin from top of page to top of text
\setlength{\topmarginheight}{2.4cm}
\newlength{\headersep} % Margin from bottom of header to top of text
\setlength{\headersep}{\topmarginheight}
\addtolength{\headersep}{-\headheight}
\addtolength{\headersep}{-1.5cm}
\RequirePackage[left=3.1cm,right=3.1cm,top=\topmarginheight,bottom=1cm,headsep=\headersep,includefoot]{geometry}

\RequirePackage{graphicx} % Used to include pictures
\graphicspath{{./images/}}

\RequirePackage{pbox} % \pbox is like \parbox but automatically determines box
                      % width
\RequirePackage{framed}

\RequirePackage{color} % used to change color of text

\RequirePackage{fancyhdr} % Allows customization of headers/footers
\renewcommand{\headrulewidth}{0pt}


%hyperref
\RequirePackage[pdfpagelabels, hidelinks,
  bookmarks=false, unicode=true, pageanchor=false,
  pdfproducer={Latex with hyperref},
  pdfcreator={pdflatex}]{hyperref}

\RequirePackage{breakurl}

%alternatives to hyperref:
%\RequirePackage{url}
%\def\url#1{\expandafter\string\csname #1\endcsname}


%URL font (times vs. courier)
%\AtBeginDocument{\urlstyle{same}}
\AtBeginDocument{\urlstyle{pcr}}


% here are all the headers
% we need to create three page styles pagefirst, pagemain and pagelast
\fancypagestyle{pagefirst}{%
  \fancyhf{}%
  \lhead{\includegraphics[height=1cm]{logoNew}}%
  % \lhead{Left top first}%
  % \rhead{Right top first}%
  % \lfoot{Left bottom first}%
  % \rfoot{Right bottom first}%
    \rhead{\raisebox{1cm-\height}
    {\begin{minipage}[t]{.48\textwidth}
      \begin{flushright}
      \fontsize{8pt}{8pt}\selectfont \sffamily\color{gray}SSN 2379-5980 (online)
      \\ \thedoi
      \end{flushright}
    \end{minipage}}
    }
}


\fancypagestyle{pagemain}{%
  \fancyhf{}%
  % \lhead{\includegraphics[height=1cm]{logoNew}}%
  % \lhead{Left top main }%
  \chead{\raisebox{1cm-\height}{LEDGER VOL \thevol~(\theyear) \ledgerpages}}
  \cfoot{\raisebox{0.25cm-\height}{\textbf{\thepage}}}
  \lfoot{\raisebox{0.5cm-\height}{\includegraphics[height=0.3cm]{logoSmall}}}%

  % \rhead{Right top main }%
  % \lfoot{Left bottom main }%
  % \rfoot{Right bottom main }%
  \rfoot{\raisebox{0.5cm-\height}
    {\begin{minipage}[t]{.48\textwidth}
      \begin{flushright}
      \fontsize{8pt}{8pt}\selectfont \sffamily\color{gray}SSN 2379-5980 (online)
      \\ \thedoi
      \end{flushright}
    \end{minipage}}}
}


\fancypagestyle{pagelast}{%
  \fancyhf{}%

  \chead{\raisebox{1cm-\height}{LEDGER VOL \thevol~(\theyear) \ledgerpages}}

  \cfoot{\raisebox{0.25cm-\height}{\textbf{\thepage}}
    \begin{minipage}[t]{\textwidth}
    {\vspace*{-5cm}\centering\includegraphics[width=\textwidth]{logoCreativeCommons}}%
    \end{minipage}
  }
  
  \lfoot{\raisebox{0.5cm-\height}{\includegraphics[height=0.3cm]{logoSmall}}}%
  \rfoot{\raisebox{0.5cm-\height}
    {\begin{minipage}[t]{.48\textwidth}
      \begin{flushright}
      \fontsize{8pt}{8pt}\selectfont \sffamily\color{gray}SSN 2379-5980 (online)
      \\ \thedoi
      \end{flushright}
    \end{minipage}}}
}


\RequirePackage{caption} % Allows customization of captions

\RequirePackage{amsmath,amssymb} % (moved earlier: cleveref must load after amsmath)
\RequirePackage{cleveref} % Used for 'smart' references



\RequirePackage{fnpct} % Change default footnote/punctuation behavior


\RequirePackage[inline]{enumitem} % Allows inline lists.  Used for keywords.
\setlist{leftmargin=1.2cm,labelindent*=.6cm,itemsep=0cm,parsep=0cm,topsep=0cm}
\setlist[enumerate]{label=(\arabic*)}
\setlist[description]{labelindent=\parindent-0.5em}



\RequirePackage{endnotes} % Used for making endnotes

\RequirePackage{amsmath,amssymb} % Math

\RequirePackage{listings} % Code
\lstset{breaklines=true,basicstyle=\ttfamily\fontsize{10pt}{11pt}\selectfont,xleftmargin=1.2cm,breakindent=.6cm}


% SPACING COMMANDS

\newcommand{\blankline}{\vspace{\baselineskip}} % Adds a blank line

% Stores the length of a single space
\newlength{\spacelength}
\settowidth{\spacelength}{\ }


% FORMATTING

\setlength{\parskip}{0pt} % So that we know exactly how much space goes between
                          % different elements

% This gets rid of extra vertical spacing in the center environment
\let\oldcenter\center
\let\oldendcenter\endcenter
\renewenvironment{center}{%
  \setlength\topsep{0pt}
  \setlength\parskip{0pt}
  \oldcenter
}{%
  \oldendcenter
}


% Redefines \maketitle to remove extra vertical space
\makeatletter
\let\old@maketitle\@maketitle
\renewcommand{\@maketitle}{\vskip -2em\old@maketitle\vspace{-20pt}}
\makeatother

% Paragraph indentation
\setlength{\parindent}{.6cm}

% Centers footnotes
\renewcommand{\footnoterule}{\noindent\hfil\rule{0.75\textwidth}{0.6pt}\hfil}


% Title and authors

\RequirePackage{titling} % Allows customization of \maketitle

\renewcommand{\and}{,\ } % Command goes between author names
\renewcommand{\thanks}[1]{\footnote{\centering \fontsize{8pt}{9pt}\selectfont
    #1\vspace{1pt}}\thanksgap{\spacelength}}
% An awkward hack to get footnote marks, punctuation, and spaces to work as
% desired
\pretitle{\centering\LARGE\bfseries} % (added: match \posttitle{\par}, avoid unbalanced center env from titling default)
\posttitle{\par}
\preauthor{\vspace{13pt}\centering\fontsize{13pt}{13pt}\selectfont
  \setfnpct{punct-after=true,add-punct-marks=\and,before-dot-space=\spacelength,before-comma-space=\spacelength}}
% Authors are centered and at 13 pt.  Continuation of awkward hack
\postauthor{\par\vspace{10pt}}
\predate{} % Removes date
\date{}
\postdate{}


% Abstract

\newlength{\abstractmargins}
\setlength{\abstractmargins}{1.2cm}
\newlength{\abstractwidth}
\setlength{\abstractwidth}{\textwidth}
\addtolength{\abstractwidth}{-2\abstractmargins}
\renewenvironment{abstract}{%
    \centering\begin{minipage}{\abstractwidth}\fontsize{10pt}{13pt}\selectfont \normalfont \textbf{Abstract.}}{\end{minipage}\par}


% Body

\renewcommand{\baselinestretch}{1.13}


% Keywords

\let\olditem\item
% (moved into keywords env to keep \item normal elsewhere)
\newlength{\keywordsmargins}
\setlength{\keywordsmargins}{1cm}
\addtolength{\keywordsmargins}{\abstractmargins}
\newlength{\keywordswidth}
\setlength{\keywordswidth}{\textwidth}
\addtolength{\keywordswidth}{-2\keywordsmargins}
\newenvironment{keywords}{\renewcommand{\item}[1]{\olditem \mbox{##1}}\vspace{9pt}
    {\centering\fontsize{10pt}{13pt}\selectfont\textsc{KEY WORDS}\par}\vspace{6pt}
    \begin{center}\begin{minipage}{\keywordswidth}\begin{center}\begin{enumerate*}[itemjoin={\hspace{1em}},label=\arabic*.]}{\end{enumerate*}\end{center}\end{minipage}\end{center}}


% Section and subsection

\RequirePackage{titlesec} % Allows customization of \section

%\setcounter{secnumdepth}{1}
%\renewcommand\thesection{\arabic{section}}


\titleformat{\section}{\fontsize{13pt}{13pt}\normalfont\bfseries}{}{0em}{\thesection .\quad}
\titlespacing*{\section}{0pt}{15pt}{7pt}


\titleformat{\subsection}[runin]{\fontshape{it}\selectfont}{}{\parindent}{\thesubsection .\quad}[---]
\titlespacing{\subsection}{0em}{0em}{0em}


\def\ledgernotes{
    %\setcounter{section}{0}
    \titleformat{\section}{\fontsize{13pt}{13pt}\normalfont\bfseries}{}{0em}{}
    \titlespacing*{\section}{0pt}{15pt}{7pt}
}


\def\appendix{
    %\setcounter{section}{0}
    \titleformat{\section}{\fontsize{13pt}{13pt}\normalfont\bfseries}{}{0em}{Appendix \Alph{section}:\quad}
    \titlespacing*{\section}{0pt}{15pt}{7pt}
}




% Tables

\RequirePackage{tabularx} % Required for nontrivial tabular environments
\newcolumntype{C}{>{\centering\arraybackslash}X} % Automatically-sized centered
                                                 % columns

\renewcommand{\tablename}{\fontsize{10pt}{13pt}\selectfont Table} % Fixes font
                                                                  % size of
                                                                  % label

\setlength{\intextsep}{0pt}    % This shrinks space before and after
\setlength{\floatsep}{0pt}     % floating environments to conform
\setlength{\textfloatsep}{0pt} % with parameters

\setlength{\extrarowheight}{2pt} % With vertical centering, this effectively
                                 % sets both the above and below "paragraph
                                 % spacing" in tables to 1pt.

% The following redefines the tabularx environment to align with the required
% parameters
\let\oldtabularx\tabularx % Stores \begin{tabularx} command
\let\oldendtabularx\endtabularx % Stores \end{tabularx} command
\renewcommand{\tabularx}[2]{\centering\minipage{#1}
  \renewcommand*{\footnoterule}{}\fontsize{10pt}{11.5pt}\selectfont\oldtabularx{#1}{#2}
}
% tabularx is now wrapped in a minipage so that footnotes appear at the bottom
% of the table

\def \endtabularx {\relax}
% A messy hack needed because of what I think is a bug in the tabularx package
% (without it, LaTeX complains on the next line that \endtabularx is not defined

\renewcommand{\endtabularx}{\vspace{-0.5em}\endminipage\oldendtabularx}
% The vertical space is to get the footnotes to display properly



% The following redefines the footnote command in minipages so that the footnote
% text displays underneath the footnote symbol when wrapping to the next line
\makeatletter
\renewcommand\@makefntext[1]{%
  %\renewcommand\@makefnmark{\hbox{\@textsuperscript{\@thefnmark}}}
  %\renewcommand\@makefnmark{\parindent 1em\noindent \hb@xt@ 1.8em{\hss \@makefnmark }#1}
  
  \fontsize{8pt}{11pt}\selectfont%
  \noindent
  \hbox{\hss\@makefnmark}\ #1}\vspace{3pt}
\makeatother

\crefname{table}{Table}{Tables} % Tells \cref how to display references to tables

\newcommand{\tableheading}[1]{\pbox[c]{\textwidth}{\vspace{4pt}\fontsize{10pt}{13pt}\selectfont#1\vspace{4pt}}} % Standards ask for "table headings" to be displayed in a certain way, and so we make a command for this


% Figures

\crefname{figure}{Fig.}{Figures} % Tells \cref how to display
\renewcommand{\figurename}{\fontsize{10pt}{13pt}\selectfont Fig.}
% In captions, changes "Figure" to "Fig." and fixes font size


% Captions
\renewcommand{\thefootnote}{\fnsymbol{footnote}}

\captionsetup{figurename=Fig.,labelsep=period,margin=1.2cm} % Changes the colon after "Table X" to a period (similarly for "Fig. X") and sets appropriate margins for captions

%\captionsetup[figure]{figurename=Fig.}

% Redefines captions to conform with standards
\makeatletter
\let\oldcaption\caption
\def\@TABLE{table}
\def\@FIGURE{figure}
\renewcommand{\caption}[1]{%
    \ifx\@captype\@TABLE
        \vspace{15pt}
    \fi
    \oldcaption{\fontsize{10pt}{13pt}\selectfont #1}
    \ifx\@captype\@TABLE
        \vspace{6pt}
    \fi
    \ifx\@captype\@FIGURE
        \vspace{20pt}
    \fi
}
\makeatother

% Mathematics

\setlength{\abovedisplayskip}{9pt}
\setlength{\abovedisplayshortskip}{9pt}
\setlength{\belowdisplayskip}{7pt}
\setlength{\belowdisplayshortskip}{7pt}

\RequirePackage{amsthm}
% \RequirePackage{ntheorem}
% \newtheorem{theorem}{Theorem}[section]
% \newtheorem{corollary}{Corollary}[theorem]
% \newtheorem{lemma}[theorem]{Lemma}
\renewcommand\qedsymbol{\hfill$\blacksquare$}
% \newtheorem*{remark}{Remark}

\newenvironment{myproof}[1][\proofname\renewcommand*{\qedsymbol}{\(\blacksquare\)}]{
  \it\hspace{-1.55em}Proof.\hspace{0.55em}\normalfont
}\qedsymbol{\endproof}



% Endnotes
% for use with \theendnotes
\renewcommand{\notesname}{Notes and References} % Changes the title of
                                                % thesection

% Changes indentation 
\renewcommand\enoteformat{\fontsize{10pt}{10pt}\selectfont \rightskip=0pt \leftskip=0pt \parindent=0.25em \parskip=0.8em \leavevmode\makeenmark \hskip0.25em}
\let\oldtheendnotes\theendnotes
\def\theendnotes{
  \titleformat{\section}{\fontsize{13pt}{13pt}\fontseries{b}\selectfont}{}{}{}
  \oldtheendnotes}


% Notes and References


% for use with bibtex
\makeatletter
\renewenvironment{thebibliography}[1]
                 %{\refname%
                 {\vspace{13pt}\par\noindent\begin{minipage}{\textwidth}\fontsize{13pt}{13pt}\selectfont \textbf{Notes and References}\end{minipage}\par  
                   %{\centering\begin{minipage}{\abstractwidth}\fontsize{13pt}{13pt}\selectfont \textbf{References and End Notes}{\end{minipage}\par}
                   \fontsize{10pt}{10pt}\selectfont
      \@mkboth{\MakeUppercase\refname}{\MakeUppercase\refname}%
      \list{\@biblabel{\@arabic\c@enumiv}}%
           {\settowidth\labelwidth{\@biblabel{#1}}%
            \leftmargin\labelwidth
            \advance\leftmargin-0.8em
            \advance\leftmargin\labelsep
            \setlength\itemindent{1em}          
            \@openbib@code
            \usecounter{enumiv}%
            \let\p@enumiv\@empty
            \renewcommand\theenumiv{\@arabic\c@enumiv}}%
      \sloppy
      \clubpenalty4000
      \@clubpenalty \clubpenalty
      \widowpenalty4000%
      \sfcode`\.\@m}
     {\def\@noitemerr
       {\@latex@warning{Empty `thebibliography' environment}}%
       \endlist}
   % \renewcommand\newblock{\hskip .11em\@plus.33em\@minus.07em}
\makeatother







