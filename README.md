# Notbad

> **A modern and *notbad* reimagining of the traditional notepad text editor, keeping the lightweight soul you love with the power features you need.**

---

O **Notbad** é um editor de texto leve e minimalista nativo para Windows (C++/Win32), inspirado diretamente no visual clássico do **Windows 10 Notepad**. O projeto nasceu a partir de uma engenharia reversa detalhada da ferramenta original da Microsoft via Ghidra, migrando o código C bruto para C++ moderno. 

Mantendo a alma veloz e a ausência de barras de ferramentas poluídas, o Notbad evoluiu para se transformar em um poderoso mini editor de código, trazendo recursos essenciais sem perder a sua leveza característica.

---

## 🚀 Principais Funcionalidades

O projeto preserva os princípios de interface clássicos (Janela de Edição, Menu e Barra de Status), mas expande as capacidades internas do editor com as seguintes implementações:

### 📑 Interface Avançada de Abas
* **Múltiplos Documentos:** Chega de abrir dezenas de janelas. O Notbad gerencia arquivos em abas através de um controle nativo com suporte a reordenação por arrastar e soltar (drag-and-drop).
* **Indicador de Modificação:** Arquivos alterados e não salvos exibem um marcador visual `*` antes do nome na aba.
* **Salvamento em Lote:** Atalho universal para Salvar Tudo (`Ctrl+Alt+S`) e fechar abas individuais de forma modular (`Ctrl+F4`).

### 📂 Painel de Arquivos Sob Demanda (TreeView)
* **Exploração de Projetos:** Ao abrir uma pasta (`Ctrl+Shift+O`), um painel lateral em árvore é renderizado dinamicamente utilizando os ícones nativos do sistema operacional. Agora é possível editar projetos inteiros sem sair do Notbad.

### 🔢 Gutter Dinâmico de Números de Linha
* **Numeração Lógica:** O gutter calcula os números de linha de forma inteligente. Linhas longas que sofrem quebra de linha automática (*Word Wrap*) compartilham o mesmo número, preservando a contagem lógica do documento.

### ✍️ Recursos Inteligentes de Edição & Código
* **Motor RichEdit Avançado:** Migração do controle Edit clássico para (RichEdit), habilitando suporte nativo a **Undo multinível** (múltiplos `Ctrl+Z`).
* **Indentação Avançada:** Diálogo exclusivo para gerenciamento de espaçamento, conversão automática de Tabs em Espaços (e vice-versa), e suporte a recuo de blocos selecionados usando `Tab` / `Shift+Tab`.

### 🔍 Busca Avançada Multi-Abas (Modeless)
* **Escopo Global:** O novo diálogo de Localizar e Substituir permite realizar buscas refinadas e substituições em lote que varrem **todos os arquivos abertos em todas as abas** simultaneamente.


## 👥 Créditos e Equipe do Projeto

O desenvolvimento e a evolução do Notbad são mantidos por:

* 👑 **Aless (MaulSmoke)** — Gerente de Projeto, Conceito & Revisão de Código
* 💻 **opencode** — Desenvolvimento do Core C++, Arquitetura Inicial & Refatoração (v141 Toolset)
* ⚡ **claudecode** — Desenvolvimento de Funcionalidades Avançadas, Otimização Win32 & Correção de Bugs

---
*Notbad Editor — Lightweight soul, power features.*
