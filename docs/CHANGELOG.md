HISTÓRICO DE MUDANÇAS EM PASSOS

==============================================
v1.0.2 — concluída em 03/11/2026

  CORREÇÕES ----------------------------
  * edição
    - Delete: tratamento explícito da tecla, apagando a seleção ou o
      caractere à direita (incluindo pares substitutos). Com auto-indentação
      ligada e estilo "Espaços", no início real da linha o Delete consome até o
      próximo nível de indentação (tabWidth espaços), limitado aos espaços
      existentes. Com auto-indentação desligada ou estilo "Tabs", apaga um
      caractere por vez. A repintura intermediária foi suprimida para não piscar
      marcando o trecho antes de apagar; o Backspace de indentação recebeu o
      mesmo tratamento.
    - Foco por clique: ao reativar uma janela inativa, o primeiro clique sobre o
      texto posiciona o cursor no ponto clicado. Clique com Shift preserva a
      seleção.
    - Atalhos de área de transferência: Shift+Delete recorta a seleção,
      Shift+Insert cola e Ctrl+Insert copia. Antes, o tratamento da tecla 
      Delete interceptava Shift+Delete e apagava sem copiar, fazendo 
      o Shift+Insert colar um conteúdo antigo. Ctrl+Delete e Alt+Delete
      continuam sendo tratados pelo RichEdit (apagar palavra etc.).
    - em um arquivo limpo, ao apertar as teclas Shift+Ctrl e soltar o arquivo ficava sujo
      
  * Interface
    - ajustado o tamanho dos componentes da barra de status conforme 
      redimensionamento da janela.
    - ao ativar modo auto-indentar é exibido na barra de status que o modo esta ativo
    - ao fechar o programa salva o estado da Tree File

  * identação
    - As configurações de indentação (ativada/desativada, largura e estilo) são
      carregadas e aplicadas ao abrir o editor, na edição corrente.
    - Adicionado o grupo "Indent using" na configuração de indentação, com os
      botões "Spaces" (padrão) e "Tabs".
    - Enter com auto-indentação copia a indentação da linha anterior já
      normalizada para o estilo configurado (espaços ou tabs).
    - Tab sem seleção insere a indentação configurada: avança até o próximo tab
      stop com espaços ou insere um tab quando o estilo é "Tabs".
    - Tab/Shift+Tab em bloco usam a unidade de indentação configurada.
    - As opções de conversão Tabs -> Spaces e Spaces -> Tabs foram mantidas.

  * números de linha
    - A numeração agora segue continuamente a posição vertical de scroll da area
      de texto com um timer curto enquanto os números estão visíveis.
      Isso cobre o scroll suave da roda do mouse, o auto-scroll ao digitar/colar,
      Enter no fim da área visível e Page Down, que antes deixavam a numeração
      defasada porque a invalidação ocorria antes do deslocamento real. O timer
      só repinta quando a posição de fato muda e é encerrado ao ocultar a numeração.

  IMPLEMENTAÇÕES -----------------------

  * menu View:
    - adicionado a opção "Highlight Current Line" no menu View, 
      com essa opção ativa a linha atual no editor é destacada 
      com um tom cinza claro.

  * menu Format:
    - adicionado a opção "Encoding" no menu Format, onde poderemos 
      definir o codec de texto na edição atual para formatos como: 
      ANSI (Win-1252), UTF-8, UTF-8 BOM, UTF-16 LE (Unicode), 
      ao escolher o formato em um texto ja editado é possivel escolher:
      ler o texto como aparece e apanas salvar no novo formato ou
      recarregar o arquivo com o novo formato. Os novos documentos
      ou arquivos vazios agora usam automaticamente o padrão UTF-8.


==============================================
v1.0.1 — concluída em 01/11/2026

  IMPLEMENTAÇÕES -----------------------
    - Migração para múltiplos documentos com abas e RichEdit.
    - Save All, Close Tab, drag-drop em nova aba, CLI substituindo a inicial.
    - Find/Replace próprio com escopo global e case sensitive.
    - TreeView sob demanda.
    - Numeração de linhas.
    - Auto-indent, indentação de bloco e conversor
      Tabs <-> Espaços.
    - Undo multinível por aba.
    - Atualização de About, versão e documentação.

==============================================
v1.0 — base inicial

  IMPLEMENTAÇÕES -----------------------
    - Recompilação do Notepad via Ghidra.
    - Versão inicial em C++ (toolset v141 / VS 2017) produzida com opencode.
    - Documento único, Edit clássico, menu e atalhos, encoding, find/replace
      nativo, impressão, fonte e configurações no registro.


